#include "retroemu/debug/cpu_selftest.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "retroemu/core/bus.hpp"
#include "retroemu/core/cartridge.hpp"
#include "retroemu/core/cpu.hpp"
#include "retroemu/core/joypad.hpp"
#include "retroemu/debug/disassembler.hpp"

namespace retroemu {
namespace {

// ---------------------------------------------------------------------------
//  A machine holding nothing but the instruction under test.
// ---------------------------------------------------------------------------
//  NOTE: the Bus and the Cpu are held by pointer rather than by value. A Bus
//  now carries 32 KiB of work RAM, 16 KiB of video RAM and a 92 KiB
//  framebuffer, so a Bench is about 150 KiB. This function declares roughly
//  forty of them in separate scopes, and AddressSanitizer deliberately stops
//  the compiler reusing one scope's stack slot for the next so it can detect
//  use-after-scope. All forty therefore coexist, which overflows the stack.
//  Putting them on the heap costs nothing here and removes the dependency on
//  a compiler optimisation.
class Bench {
public:
    Bench() : bus_(new Bus), cpu_(new Cpu) {}
    // `code` is placed at the entry point, 0x0100.
    bool load(const std::vector<u8> &code)
    {
        std::vector<u8> rom(32 * 1024, 0x00);
        for (std::size_t i = 0; i < code.size(); ++i) rom[0x0100 + i] = code[i];

        // A minimal valid header: ROM only, 32 KiB, no RAM, correct checksum.
        rom[0x0147] = 0x00;
        rom[0x0148] = 0x00;
        rom[0x0149] = 0x00;
        u8 sum = 0;
        for (std::size_t a = 0x0134; a <= 0x014C; ++a) sum = static_cast<u8>(sum - rom[a] - 1);
        rom[0x014D] = sum;

        Cartridge   cart;
        std::string error;
        if (!cart.load_from_memory(std::move(rom), "<selftest>", error)) return false;

        bus_->attach(std::move(cart), Model::Dmg);
        cpu_->reset(Model::Dmg);
        return true;
    }

    void step() { cpu_->step(*bus_); }

    Cpu &cpu() { return *cpu_; }
    Bus &bus() { return *bus_; }

    // T-cycles consumed by the last step, as charged by the bus.
    u32 last_cycles()
    {
        const u64 before = bus_->clock().t_cpu();
        cpu_->step(*bus_);
        return static_cast<u32>(bus_->clock().t_cpu() - before);
    }

private:
    std::unique_ptr<Bus> bus_;
    std::unique_ptr<Cpu> cpu_;
};

int failures = 0;
int checks   = 0;
bool verbose_ = false;

void report(bool ok, const std::string &name, const std::string &detail)
{
    ++checks;
    if (ok) {
        if (verbose_) std::printf("  \033[1;32mPASS\033[0m  %s\n", name.c_str());
        return;
    }
    ++failures;
    std::printf("  \033[1;31mFAIL\033[0m  %s\n        %s\n", name.c_str(), detail.c_str());
}

std::string flags_to_text(u8 f)
{
    std::string s;
    s += (f & FlagZ) ? 'Z' : '-';
    s += (f & FlagN) ? 'N' : '-';
    s += (f & FlagH) ? 'H' : '-';
    s += (f & FlagC) ? 'C' : '-';
    return s;
}

u8 text_to_flags(const char *text)
{
    u8 f = 0;
    if (text[0] == 'Z') f |= FlagZ;
    if (text[1] == 'N') f |= FlagN;
    if (text[2] == 'H') f |= FlagH;
    if (text[3] == 'C') f |= FlagC;
    return f;
}

// ---------------------------------------------------------------------------
//  ALU cases: run one opcode with A, a second operand and an incoming carry,
//  then check A and all four flags.
// ---------------------------------------------------------------------------
struct AluCase {
    const char *name;
    u8          opcode;        // operates on B, except the immediate forms
    u8          a_in;
    u8          b_in;
    bool        carry_in;
    u8          a_out;
    const char *flags_out;     // four characters, e.g. "--H-"
};

void run_alu_case(const AluCase &c)
{
    Bench bench;
    if (!bench.load({c.opcode})) { report(false, c.name, "bench failed to build"); return; }

    bench.cpu().regs().a = c.a_in;
    bench.cpu().regs().b = c.b_in;
    bench.cpu().regs().f = c.carry_in ? FlagC : 0;
    bench.step();

    const u8 got_a = bench.cpu().regs().a;
    const u8 got_f = bench.cpu().regs().f;
    const u8 want_f = text_to_flags(c.flags_out);

    char detail[192];
    std::snprintf(detail, sizeof(detail),
                  "A=0x%02X %s   expected A=0x%02X %s   (inputs A=0x%02X B=0x%02X C=%d)",
                  got_a, flags_to_text(got_f).c_str(),
                  c.a_out, flags_to_text(want_f).c_str(),
                  c.a_in, c.b_in, c.carry_in ? 1 : 0);

    report(got_a == c.a_out && got_f == want_f, c.name, detail);
}

void check_u16(const char *name, u16 got, u16 want)
{
    char detail[128];
    std::snprintf(detail, sizeof(detail), "got 0x%04X, expected 0x%04X", got, want);
    report(got == want, name, detail);
}

void check_u8(const char *name, u8 got, u8 want)
{
    char detail[128];
    std::snprintf(detail, sizeof(detail), "got 0x%02X, expected 0x%02X", got, want);
    report(got == want, name, detail);
}

void check_flags(const char *name, u8 got, const char *want_text)
{
    const u8 want = text_to_flags(want_text);
    char detail[128];
    std::snprintf(detail, sizeof(detail), "flags %s, expected %s",
                  flags_to_text(got).c_str(), flags_to_text(want).c_str());
    report(got == want, name, detail);
}

void check_text(const char *name, const std::string &got, const char *want)
{
    char detail[192];
    std::snprintf(detail, sizeof(detail), "got \"%s\", expected \"%s\"", got.c_str(), want);
    report(got == want, name, detail);
}

void check_cycles(const char *name, u32 got, u32 want)
{
    char detail[128];
    std::snprintf(detail, sizeof(detail), "took %u T-cycles, expected %u", got, want);
    report(got == want, name, detail);
}

}  // namespace

// ---------------------------------------------------------------------------
int run_cpu_selftest(bool verbose)
{
    verbose_ = verbose;
    failures = 0;
    checks   = 0;

    // === Arithmetic and the half-carry =====================================
    if (verbose) std::printf("\n== arithmetic and the half-carry ==\n");
    const AluCase alu_cases[] = {
        // ADD A,B  (0x80)
        {"ADD A,B  half-carry only",      0x80, 0x0F, 0x01, false, 0x10, "--H-"},
        {"ADD A,B  zero, half and carry", 0x80, 0xFF, 0x01, false, 0x00, "Z-HC"},
        {"ADD A,B  carry without half",   0x80, 0xF0, 0x10, false, 0x00, "Z--C"},
        {"ADD A,B  no flag at all",       0x80, 0x3A, 0x05, false, 0x3F, "----"},
        {"ADD A,B  ignores incoming C",   0x80, 0x00, 0x00, true,  0x00, "Z---"},
        // ADC A,B  (0x88)
        {"ADC A,B  adds the carry in",    0x88, 0x0E, 0x01, true,  0x10, "--H-"},
        {"ADC A,B  carry makes it wrap",  0x88, 0xFF, 0x00, true,  0x00, "Z-HC"},
        // SUB B    (0x90)
        {"SUB B    borrow from nothing",  0x90, 0x00, 0x01, false, 0xFF, "-NHC"},
        {"SUB B    exact zero",           0x90, 0x10, 0x10, false, 0x00, "ZN--"},
        {"SUB B    half-borrow only",     0x90, 0x10, 0x01, false, 0x0F, "-NH-"},
        // SBC A,B  (0x98)
        {"SBC A,B  subtracts the carry",  0x98, 0x10, 0x00, true,  0x0F, "-NH-"},
        {"SBC A,B  full borrow",          0x98, 0x00, 0x00, true,  0xFF, "-NHC"},
        // Logic
        {"AND B    sets H, clears C",     0xA0, 0xF0, 0x0F, true,  0x00, "Z-H-"},
        {"AND B    non-zero result",      0xA0, 0x3C, 0x0F, false, 0x0C, "--H-"},
        {"XOR B    clears every flag",    0xA8, 0xFF, 0xFF, true,  0x00, "Z---"},
        {"OR  B    clears every flag",    0xB0, 0xF0, 0x0F, true,  0xFF, "----"},
        // CP       (0xB8): a subtraction that keeps A
        {"CP  B    leaves A alone",       0xB8, 0x10, 0x20, false, 0x10, "-N-C"},
        {"CP  B    equal means zero",     0xB8, 0x42, 0x42, false, 0x42, "ZN--"},
    };
    for (const AluCase &c : alu_cases) run_alu_case(c);

    // === INC and DEC leave the carry flag untouched =========================
    if (verbose) std::printf("\n== INC / DEC preserve the carry ==\n");
    {
        Bench b;
        b.load({0x04});                       // INC B
        b.cpu().regs().b = 0x0F;
        b.cpu().regs().f = FlagC;             // carry set on the way in
        b.step();
        check_u8("INC B  0x0F -> 0x10", b.cpu().regs().b, 0x10);
        check_flags("INC B  keeps the carry", b.cpu().regs().f, "--HC");
    }
    {
        Bench b;
        b.load({0x05});                       // DEC B
        b.cpu().regs().b = 0x00;
        b.cpu().regs().f = 0;
        b.step();
        check_u8("DEC B  0x00 -> 0xFF", b.cpu().regs().b, 0xFF);
        check_flags("DEC B  sets N and H", b.cpu().regs().f, "-NH-");
    }

    // === 16-bit arithmetic: the half-carry moves to bit 11 ==================
    if (verbose) std::printf("\n== 16-bit arithmetic ==\n");
    {
        Bench b;
        b.load({0x09});                       // ADD HL,BC
        b.cpu().regs().set_hl(0x0FFF);
        b.cpu().regs().set_bc(0x0001);
        b.cpu().regs().f = FlagZ;             // Z must survive untouched
        b.step();
        check_u16("ADD HL,BC  0x0FFF + 1", b.cpu().regs().hl(), 0x1000);
        check_flags("ADD HL,BC  H at bit 11, Z untouched", b.cpu().regs().f, "Z-H-");
    }
    {
        Bench b;
        b.load({0xE8, 0x01});                 // ADD SP,+1
        b.cpu().regs().sp = 0x000F;
        b.step();
        check_u16("ADD SP,+1", b.cpu().regs().sp, 0x0010);
        check_flags("ADD SP,e8  flags come from the low byte", b.cpu().regs().f, "--H-");
    }
    {
        Bench b;
        b.load({0xF8, 0xFF});                 // LD HL,SP-1
        b.cpu().regs().sp = 0x0000;
        b.step();
        check_u16("LD HL,SP-1  wraps around", b.cpu().regs().hl(), 0xFFFF);
    }

    // === DAA, the instruction that reads N and H ============================
    if (verbose) std::printf("\n== DAA ==\n");
    {
        Bench b;
        b.load({0x80, 0x27});                 // ADD A,B then DAA
        b.cpu().regs().a = 0x09;
        b.cpu().regs().b = 0x08;              // 9 + 8 = 17 in decimal
        b.step();                             // A = 0x11, H set
        b.step();                             // DAA -> 0x17
        check_u8("DAA after ADD  9 + 8 = 17", b.cpu().regs().a, 0x17);
    }
    {
        Bench b;
        b.load({0x90, 0x27});                 // SUB B then DAA
        b.cpu().regs().a = 0x10;
        b.cpu().regs().b = 0x01;              // 10 - 1 = 9 in decimal
        b.step();
        b.step();
        check_u8("DAA after SUB  10 - 1 = 09", b.cpu().regs().a, 0x09);
    }

    // === Rotates: the one-byte forms always clear Z =========================
    if (verbose) std::printf("\n== rotates and shifts ==\n");
    {
        Bench b;
        b.load({0x07});                       // RLCA
        b.cpu().regs().a = 0x00;
        b.step();
        check_flags("RLCA clears Z even on a zero result", b.cpu().regs().f, "----");
    }
    {
        Bench b;
        b.load({0xCB, 0x07});                 // RLC A
        b.cpu().regs().a = 0x00;
        b.step();
        check_flags("RLC A sets Z on a zero result", b.cpu().regs().f, "Z---");
    }
    {
        Bench b;
        b.load({0xCB, 0x20});                 // SLA B  (y=4 SLA, z=0 B)
        b.cpu().regs().b = 0x80;
        b.step();
        check_u8("SLA B  0x80 -> 0x00", b.cpu().regs().b, 0x00);
        check_flags("SLA B  shifts bit 7 into the carry", b.cpu().regs().f, "Z--C");
    }
    {
        Bench b;
        b.load({0xCB, 0x28});                 // SRA B: arithmetic, keeps bit 7
        b.cpu().regs().b = 0x81;
        b.step();
        check_u8("SRA B  keeps bit 7", b.cpu().regs().b, 0xC0);
    }
    {
        Bench b;
        b.load({0xCB, 0x38});                 // SRL B: logical, clears bit 7
        b.cpu().regs().b = 0x81;
        b.step();
        check_u8("SRL B  clears bit 7", b.cpu().regs().b, 0x40);
    }
    {
        Bench b;
        b.load({0xCB, 0x30});                 // SWAP B
        b.cpu().regs().b = 0xAB;
        b.step();
        check_u8("SWAP B  swaps the nibbles", b.cpu().regs().b, 0xBA);
    }
    {
        Bench b;
        b.load({0xCB, 0x40});                 // BIT 0,B
        b.cpu().regs().b = 0x01;
        b.cpu().regs().f = FlagC;
        b.step();
        check_flags("BIT leaves the carry alone", b.cpu().regs().f, "--HC");
    }

    // === F has no low nibble ================================================
    if (verbose) std::printf("\n== the F register ==\n");
    {
        Bench b;
        b.load({0xF1});                       // POP AF
        b.cpu().regs().sp = 0xC000;
        b.bus().write(0xC000, 0xFF);          // try to push junk into F
        b.bus().write(0xC001, 0x12);
        b.step();
        check_u8("POP AF  discards the low nibble of F", b.cpu().regs().f, 0xF0);
        check_u8("POP AF  loads A", b.cpu().regs().a, 0x12);
    }

    // === Control flow ======================================================
    if (verbose) std::printf("\n== control flow ==\n");
    {
        Bench b;
        b.load({0x18, 0xFE});                 // JR -2: the classic infinite loop
        b.step();
        check_u16("JR -2 jumps backwards onto itself", b.cpu().regs().pc, 0x0100);
    }
    {
        Bench b;
        b.load({0xC7});                       // RST 00
        b.cpu().regs().sp = 0xC002;
        b.step();
        check_u16("RST 00 jumps to 0x0000", b.cpu().regs().pc, 0x0000);
        check_u16("RST 00 pushes the return address",
                  static_cast<u16>((b.bus().peek(0xC001) << 8) | b.bus().peek(0xC000)), 0x0101);
    }
    {
        Bench b;
        b.load({0xCD, 0x34, 0x12});           // CALL 0x1234
        b.cpu().regs().sp = 0xC002;
        b.step();
        check_u16("CALL jumps to the target", b.cpu().regs().pc, 0x1234);
        check_u16("CALL pushes the address after it",
                  static_cast<u16>((b.bus().peek(0xC001) << 8) | b.bus().peek(0xC000)), 0x0103);
    }

    // === Timing, as charged by the bus (decision D8) ========================
    if (verbose) std::printf("\n== instruction timing ==\n");
    {
        Bench b; b.load({0x00});              check_cycles("NOP", b.last_cycles(), 4); }
    {
        Bench b; b.load({0x06, 0x42});        check_cycles("LD B,n", b.last_cycles(), 8); }
    {
        Bench b; b.load({0x01, 0x34, 0x12});  check_cycles("LD BC,nn", b.last_cycles(), 12); }
    {
        Bench b; b.load({0x03});              check_cycles("INC BC (internal cycle)", b.last_cycles(), 8); }
    {
        Bench b; b.load({0x34});              b.cpu().regs().set_hl(0xC000);
        check_cycles("INC (HL)", b.last_cycles(), 12); }
    {
        Bench b; b.load({0x18, 0x00});        check_cycles("JR taken", b.last_cycles(), 12); }
    {
        Bench b; b.load({0x20, 0x00});        b.cpu().regs().f = FlagZ;
        check_cycles("JR NZ not taken", b.last_cycles(), 8); }
    {
        Bench b; b.load({0xC3, 0x00, 0x01});  check_cycles("JP nn", b.last_cycles(), 16); }
    {
        Bench b; b.load({0xCD, 0x00, 0x01});  b.cpu().regs().sp = 0xC002;
        check_cycles("CALL nn", b.last_cycles(), 24); }
    {
        Bench b; b.load({0xC9});              b.cpu().regs().sp = 0xC000;
        check_cycles("RET", b.last_cycles(), 16); }
    {
        Bench b; b.load({0xC0});              b.cpu().regs().sp = 0xC000; b.cpu().regs().f = FlagZ;
        check_cycles("RET NZ not taken", b.last_cycles(), 8); }
    {
        Bench b; b.load({0xC5});              b.cpu().regs().sp = 0xC002;
        check_cycles("PUSH BC", b.last_cycles(), 16); }
    {
        Bench b; b.load({0xC1});              b.cpu().regs().sp = 0xC000;
        check_cycles("POP BC", b.last_cycles(), 12); }
    {
        Bench b; b.load({0xE8, 0x01});        check_cycles("ADD SP,e8", b.last_cycles(), 16); }
    {
        Bench b; b.load({0xF8, 0x01});        check_cycles("LD HL,SP+e8", b.last_cycles(), 12); }
    {
        Bench b; b.load({0xCB, 0x40});        check_cycles("BIT 0,B", b.last_cycles(), 8); }
    {
        Bench b; b.load({0xCB, 0x46});        b.cpu().regs().set_hl(0xC000);
        check_cycles("BIT 0,(HL) reads only", b.last_cycles(), 12); }
    {
        Bench b; b.load({0xCB, 0x86});        b.cpu().regs().set_hl(0xC000);
        check_cycles("RES 0,(HL) reads and writes", b.last_cycles(), 16); }

    // === Interrupts ========================================================
    if (verbose) std::printf("\n== interrupts ==\n");
    {
        Bench b;
        b.load({0xFB, 0x00, 0x00});           // EI ; NOP ; NOP
        b.step();                             // EI itself
        report(!b.cpu().ime(), "EI does not take effect immediately", "IME was already set");
        b.step();                             // the instruction after EI
        report(b.cpu().ime(), "EI takes effect one instruction later", "IME never got set");
    }
    {
        Bench b;
        b.load({0xFB, 0x00, 0x00});
        b.bus().write(0xFFFF, IntVBlank);     // enable VBlank
        b.bus().request_interrupt(IntVBlank);
        b.step();                             // EI
        b.step();                             // NOP, IME becomes set afterwards
        const u32 cycles = b.last_cycles();   // this step services the interrupt
        check_u16("an interrupt jumps to its vector", b.cpu().regs().pc, 0x0040);
        check_cycles("dispatch costs 5 machine cycles", cycles, 20);
        report(!b.cpu().ime(), "dispatch clears IME", "IME was still set");
    }
    {
        Bench b;
        b.load({0x76, 0x00});                 // HALT
        b.bus().write(0xFFFF, IntVBlank);
        b.bus().set_interrupt_flags(0);       // IF reads 0xE1 after boot: clear it,
                                              // otherwise HALT hits the halt bug below
        b.step();
        report(b.cpu().halted(), "HALT stops the CPU", "the CPU did not halt");
        b.bus().request_interrupt(IntVBlank);
        b.step();
        report(!b.cpu().halted(), "a pending interrupt wakes HALT even with IME clear",
               "the CPU stayed halted");
    }
    {
        // The halt bug: with IME clear and an interrupt ALREADY pending, HALT
        // does not stop the CPU at all.
        Bench b;
        b.load({0x76, 0x3C});                 // HALT ; INC A
        b.bus().write(0xFFFF, IntVBlank);
        b.bus().request_interrupt(IntVBlank); // pending before HALT runs
        b.step();
        report(!b.cpu().halted(), "HALT does not stop when an interrupt is already pending",
               "the CPU halted anyway");
    }

    // === Timer ==============================================================
    //  DIV is the upper byte of one internal 16-bit counter, and TIMA counts
    //  the falling edges of a selected bit of that same counter. Everything
    //  surprising about this component follows from those two facts.
    if (verbose) std::printf("\n== timer ==\n");
    {
        Bench b; b.load({0x00});
        check_u8("DIV reads 0xAB after boot", b.bus().timer().div(), 0xAB);
    }
    {
        // DIV is the counter's high byte, so it moves once every 256 cycles.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF04, 0x00);                 // reset the counter
        const u8 before = b.bus().timer().div();
        b.bus().tick(255);
        check_u8("DIV has not moved after 255 cycles", b.bus().timer().div(), before);
        b.bus().tick(1);
        check_u8("DIV moves on the 256th", b.bus().timer().div(), static_cast<u8>(before + 1));
    }
    {
        // Writing to DIV does not store the value: it zeroes the whole counter.
        Bench b; b.load({0x00});
        b.bus().tick(1000);
        b.bus().poke(0xFF04, 0x37);
        check_u8("writing to DIV resets it to zero", b.bus().timer().div(), 0x00);
    }
    {
        // Each TAC setting watches a different bit, so TIMA ticks at a
        // different rate. Note the order: 01 is the FASTEST, not the slowest.
        struct Rate { u8 tac; u32 cycles; const char *name; };
        const Rate rates[] = {
            {0x05, 16,   "TAC=01 increments TIMA every 16 cycles"},
            {0x06, 64,   "TAC=10 increments TIMA every 64 cycles"},
            {0x07, 256,  "TAC=11 increments TIMA every 256 cycles"},
            {0x04, 1024, "TAC=00 increments TIMA every 1024 cycles"},
        };
        for (const Rate &rate : rates) {
            Bench b; b.load({0x00});
            // Order matters. Resetting DIV can itself increment TIMA (that
            // is the quirk tested further down), so TIMA is zeroed AFTER the
            // counter, never before.
            b.bus().poke(0xFF07, rate.tac);
            b.bus().poke(0xFF04, 0x00);             // counter to zero
            b.bus().poke(0xFF05, 0x00);
            b.bus().tick(rate.cycles - 1);
            const bool early = b.bus().peek(0xFF05) != 0x00;
            b.bus().tick(1);
            const bool ontime = b.bus().peek(0xFF05) == 0x01;
            report(!early && ontime, rate.name,
                   early ? "TIMA moved too early" : "TIMA did not move on time");
        }
    }
    {
        // Bit 2 of TAC is the enable. With it clear, nothing happens at all.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF07, 0x01);                 // fastest rate, but disabled
        b.bus().poke(0xFF04, 0x00);
        b.bus().poke(0xFF05, 0x00);
        b.bus().tick(4096);
        check_u8("a disabled timer never increments TIMA", b.bus().peek(0xFF05), 0x00);
    }
    {
        // The consequence nobody expects: resetting DIV while the watched bit
        // is high is itself a falling edge, so TIMA increments.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF07, 0x05);                 // watch bit 3
        b.bus().poke(0xFF04, 0x00);                 // counter to zero first
        b.bus().poke(0xFF05, 0x00);                 // then TIMA
        b.bus().tick(8);                            // bit 3 is now 1
        b.bus().poke(0xFF04, 0x00);                 // reset: bit 3 falls to 0
        check_u8("resetting DIV can increment TIMA", b.bus().peek(0xFF05), 0x01);
    }
    {
        // Overflow: TIMA reloads from TMA and requests the timer interrupt.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF07, 0x05);                 // fastest rate
        b.bus().poke(0xFF04, 0x00);                 // counter to zero first
        b.bus().poke(0xFF06, 0x7E);                 // TMA
        b.bus().poke(0xFF05, 0xFF);                 // TIMA about to overflow
        b.bus().set_interrupt_flags(0);
        b.bus().tick(16);                           // one increment: 0xFF -> overflow
        check_u8("TIMA reads 0x00 for one cycle after overflowing", b.bus().peek(0xFF05), 0x00);
        report((b.bus().interrupt_flags() & IntTimer) == 0,
               "no interrupt yet during that window", "the interrupt fired too early");
        b.bus().tick(4);
        check_u8("then TMA is copied into TIMA", b.bus().peek(0xFF05), 0x7E);
        report((b.bus().interrupt_flags() & IntTimer) != 0,
               "and the timer interrupt is requested", "the interrupt never fired");
    }
    {
        // Writing to TIMA inside the reload window cancels both.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF07, 0x05);
        b.bus().poke(0xFF04, 0x00);
        b.bus().poke(0xFF06, 0x7E);
        b.bus().poke(0xFF05, 0xFF);
        b.bus().set_interrupt_flags(0);
        b.bus().tick(16);                           // overflow, reload pending
        b.bus().poke(0xFF05, 0x11);                 // cancel it
        b.bus().tick(8);
        check_u8("writing TIMA during the window cancels the reload",
                 b.bus().peek(0xFF05), 0x11);
        report((b.bus().interrupt_flags() & IntTimer) == 0,
               "and cancels the interrupt with it", "the interrupt fired anyway");
    }
    {
        // The top five bits of TAC are not wired and read as 1.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF07, 0x05);
        check_u8("unused TAC bits read as 1", b.bus().peek(0xFF07), 0xFD);
    }

    // === PPU state machine ==================================================
    //  The screen is swept line by line, 456 dots each, cycling through three
    //  modes on the visible lines and sitting in VBlank for ten more.
    if (verbose) std::printf("\n== PPU state machine ==\n");
    {
        Bench b; b.load({0x00});
        check_u8("the screen is on after boot", b.bus().ppu().lcd_on() ? 1 : 0, 1);
        check_u8("LY starts at 0", b.bus().ppu().ly(), 0);
        report(b.bus().ppu().mode() == PpuMode::OamScan,
               "a line starts in mode 2, the OAM scan", "wrong starting mode");
    }
    {
        // The three modes of a visible line, at their documented boundaries.
        Bench b; b.load({0x00});
        b.bus().tick(79);
        report(b.bus().ppu().mode() == PpuMode::OamScan,
               "mode 2 lasts 80 dots", "left mode 2 too early");
        b.bus().tick(1);
        report(b.bus().ppu().mode() == PpuMode::Drawing,
               "then mode 3 begins", "did not enter mode 3");
        b.bus().tick(171);
        report(b.bus().ppu().mode() == PpuMode::Drawing,
               "mode 3 lasts 172 dots", "left mode 3 too early");
        b.bus().tick(1);
        report(b.bus().ppu().mode() == PpuMode::HBlank,
               "then mode 0, HBlank", "did not enter HBlank");
    }
    {
        Bench b; b.load({0x00});
        b.bus().tick(455);
        check_u8("LY has not moved after 455 dots", b.bus().ppu().ly(), 0);
        b.bus().tick(1);
        check_u8("a line is exactly 456 dots", b.bus().ppu().ly(), 1);
    }
    {
        // 144 visible lines, then VBlank.
        Bench b; b.load({0x00});
        b.bus().set_interrupt_flags(0);
        b.bus().tick(144 * kDotsPerLine - 1);
        check_u8("LY is 143 on the last visible line", b.bus().ppu().ly(), 143);
        report((b.bus().interrupt_flags() & IntVBlank) == 0,
               "no VBlank interrupt yet", "VBlank fired too early");
        b.bus().tick(1);
        check_u8("LY reaches 144", b.bus().ppu().ly(), 144);
        report(b.bus().ppu().mode() == PpuMode::VBlank,
               "and the PPU enters VBlank", "did not enter VBlank");
        report((b.bus().interrupt_flags() & IntVBlank) != 0,
               "the VBlank interrupt is requested", "VBlank never fired");
    }
    {
        // 154 lines in total: 144 visible plus 10 of VBlank.
        Bench b; b.load({0x00});
        b.bus().tick(153 * kDotsPerLine);
        check_u8("LY reaches 153, the last line", b.bus().ppu().ly(), 153);
        b.bus().tick(kDotsPerLine);
        check_u8("then wraps back to 0", b.bus().ppu().ly(), 0);
        check_u8("one frame was completed", static_cast<u8>(b.bus().ppu().frames()), 1);
    }
    {
        // 154 x 456 = 70224 dots, which is 59.727 frames per second.
        Bench b; b.load({0x00});
        b.bus().tick(kTCyclesPerFrame);
        check_u8("a frame is 70224 dots", static_cast<u8>(b.bus().ppu().frames()), 1);
        check_u8("and LY is back at 0", b.bus().ppu().ly(), 0);
    }
    {
        // The refresh rate, measured with nothing else interfering: one
        // emulated second is 4194304 dots, which is 59.727 frames.
        Bench b; b.load({0x00});
        b.bus().tick(kSystemClockHz);
        check_u8("59 frames are drawn in one emulated second",
                 static_cast<u8>(b.bus().ppu().frames()), 59);
    }
    {
        // STAT reports the mode and the LY==LYC comparison, whatever was
        // written to it. Bit 7 is not wired and reads as 1.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF45, 0x00);                 // LYC = 0, and LY is 0
        const u8 stat = b.bus().peek(0xFF41);
        report((stat & 0x80) != 0, "STAT bit 7 reads as 1", "bit 7 was clear");
        report((stat & 0x04) != 0, "STAT reports LY == LYC", "the LYC flag was clear");
        report((stat & 0x03) == static_cast<u8>(PpuMode::OamScan),
               "STAT reports the current mode", "wrong mode in STAT");
        b.bus().poke(0xFF45, 0x42);                 // LYC no longer matches
        report((b.bus().peek(0xFF41) & 0x04) == 0,
               "and clears the flag when they differ", "the LYC flag stayed set");
    }
    {
        // The STAT interrupt fires on a RISING edge of the OR of its enabled
        // sources, so two overlapping events give one interrupt, not two.
        // Order matters: LY is 0 at reset, so setting LYC to 5 BEFORE enabling
        // the source avoids an immediate match. Enabling first would raise the
        // interrupt straight away, which is correct hardware behaviour and is
        // checked separately below.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF45, 0x05);                 // fire when LY reaches 5
        b.bus().poke(0xFF41, 0x40);                 // enable the LYC source
        b.bus().set_interrupt_flags(0);
        b.bus().tick(4 * kDotsPerLine);
        report((b.bus().interrupt_flags() & IntStat) == 0,
               "no STAT interrupt before the match", "STAT fired too early");
        b.bus().tick(kDotsPerLine);                 // LY becomes 5
        report((b.bus().interrupt_flags() & IntStat) != 0,
               "a STAT interrupt on the LYC match", "STAT never fired");
        b.bus().set_interrupt_flags(0);
        b.bus().tick(10);                           // still on line 5
        report((b.bus().interrupt_flags() & IntStat) == 0,
               "and only once while the match holds", "STAT fired again on the same line");
    }
    {
        // Enabling a STAT source while its condition is ALREADY true is a
        // rising edge of the line, so the interrupt fires immediately. Real
        // behaviour, and a classic source of unexpected interrupts.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF45, 0x00);                 // LYC = 0, and LY is already 0
        b.bus().set_interrupt_flags(0);
        b.bus().poke(0xFF41, 0x40);                 // enable the source now
        b.bus().tick(4);
        report((b.bus().interrupt_flags() & IntStat) != 0,
               "enabling a source whose condition already holds fires at once",
               "no interrupt was raised");
    }
    {
        // Turning the screen off stops the sweep. Games do this before
        // rewriting video memory in bulk.
        Bench b; b.load({0x00});
        b.bus().tick(3 * kDotsPerLine);
        b.bus().poke(0xFF40, 0x11);                 // clear bit 7
        check_u8("LY reads 0 while the screen is off", b.bus().peek(0xFF44), 0);
        b.bus().tick(10 * kDotsPerLine);
        check_u8("and stays there: nothing is swept", b.bus().peek(0xFF44), 0);
        b.bus().poke(0xFF40, 0x91);                 // back on
        b.bus().tick(kDotsPerLine);
        check_u8("turning it back on restarts the sweep", b.bus().peek(0xFF44), 1);
    }
    {
        Bench b; b.load({0x00});
        b.bus().tick(2 * kDotsPerLine);
        const u8 before = b.bus().peek(0xFF44);
        b.bus().poke(0xFF44, 0x77);
        check_u8("LY is read-only", b.bus().peek(0xFF44), before);
    }

    // === Joypad (subject V.4) ===============================================
    //  Eight buttons through one register that shows four wires at a time.
    //  The game clears a selection bit to choose which half it sees. The
    //  logic is inverted throughout: a bit reads 0 when the button is DOWN.
    if (verbose) std::printf("\n== joypad ==\n");
    {
        Bench b; b.load({0x00});
        check_u8("JOYP reads 0xCF after boot", b.bus().peek(0xFF00), 0xCF);
    }
    {
        Bench b; b.load({0x00});
        b.bus().poke(0xFF00, 0x10);          // select the action buttons
        check_u8("nothing pressed reads all ones", b.bus().peek(0xFF00), 0xDF);
        b.bus().joypad().set(ButtonA, true);
        check_u8("pressing A clears bit 0", b.bus().peek(0xFF00), 0xDE);
        b.bus().joypad().set(ButtonStart, true);
        check_u8("pressing Start clears bit 3", b.bus().peek(0xFF00), 0xD6);
        b.bus().joypad().set(ButtonA, false);
        check_u8("releasing A sets it back", b.bus().peek(0xFF00), 0xD7);
    }
    {
        Bench b; b.load({0x00});
        b.bus().poke(0xFF00, 0x20);          // select the control pad
        b.bus().joypad().set(ButtonRight, true);
        check_u8("pressing Right clears bit 0", b.bus().peek(0xFF00), 0xEE);
        b.bus().joypad().set(ButtonDown, true);
        check_u8("pressing Down clears bit 3", b.bus().peek(0xFF00), 0xE6);
    }
    {
        // A button of the half that is NOT selected stays invisible. This is
        // why reading all eight takes two writes and two reads.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF00, 0x10);          // action buttons selected
        b.bus().joypad().set(ButtonRight, true);
        check_u8("a button of the other half does not show", b.bus().peek(0xFF00), 0xDF);
        b.bus().poke(0xFF00, 0x20);          // now the control pad
        check_u8("and appears once its half is selected", b.bus().peek(0xFF00), 0xEE);
    }
    {
        // Selecting both halves is allowed: the wires carry both, ANDed,
        // because a pressed button in either half pulls the same wire low.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF00, 0x00);          // both halves
        b.bus().joypad().set(ButtonA, true);       // bit 0 of the action half
        b.bus().joypad().set(ButtonLeft, true);    // bit 1 of the pad half
        check_u8("both halves at once are combined", b.bus().peek(0xFF00), 0xCC);
    }
    {
        Bench b; b.load({0x00});
        b.bus().poke(0xFF00, 0x30);          // neither half
        b.bus().joypad().set(ButtonA, true);
        b.bus().joypad().set(ButtonRight, true);
        check_u8("with neither half selected nothing shows", b.bus().peek(0xFF00), 0xFF);
    }
    {
        Bench b; b.load({0x00});
        b.bus().poke(0xFF00, 0x00);
        report((b.bus().peek(0xFF00) & 0xC0) == 0xC0,
               "the two unused bits always read as 1", "bits 6 or 7 were clear");
        b.bus().poke(0xFF00, 0xFF);
        report((b.bus().peek(0xFF00) & 0x0F) == 0x0F,
               "writing cannot set the button bits", "a write reached the button wires");
    }
    {
        // The interrupt fires when a wire falls, so only for a button of the
        // selected half, and only on the press.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF00, 0x10);          // action buttons selected
        b.bus().tick(4);
        b.bus().set_interrupt_flags(0);

        b.bus().joypad().set(ButtonRight, true);   // the other half
        b.bus().tick(4);
        report((b.bus().interrupt_flags() & IntJoypad) == 0,
               "no interrupt for a button of the deselected half", "an interrupt fired");

        b.bus().joypad().set(ButtonA, true);
        b.bus().tick(4);
        report((b.bus().interrupt_flags() & IntJoypad) != 0,
               "an interrupt when a selected button is pressed", "no interrupt fired");

        b.bus().set_interrupt_flags(0);
        b.bus().joypad().set(ButtonA, false);
        b.bus().tick(4);
        report((b.bus().interrupt_flags() & IntJoypad) == 0,
               "and none when it is released", "releasing raised an interrupt");
    }

    // === OAM DMA ============================================================
    //  Writing one byte to 0xFF46 starts a 160-byte copy into the sprite
    //  table that runs on its own, one byte per machine cycle.
    if (verbose) std::printf("\n== OAM DMA ==\n");
    {
        Bench b; b.load({0x00});
        // Fill the source page with a recognisable pattern.
        for (int i = 0; i < 160; ++i)
            b.bus().poke(static_cast<u16>(0xC000 + i), static_cast<u8>(0xA0 + i));
        for (int i = 0; i < 160; ++i) b.bus().poke(static_cast<u16>(0xFE00 + i), 0x00);

        b.bus().poke(0xFF46, 0xC0);     // copy from 0xC000
        report(b.bus().dma().active(), "writing 0xFF46 starts a transfer",
               "nothing started");

        b.bus().tick(160 * 4 + 4);      // 160 machine cycles, plus the startup one
        report(!b.bus().dma().active(), "it finishes after 160 machine cycles",
               "the transfer was still running");

        bool ok = true;
        for (int i = 0; i < 160; ++i)
            if (b.bus().peek(static_cast<u16>(0xFE00 + i)) != static_cast<u8>(0xA0 + i)) ok = false;
        report(ok, "all 160 bytes land in the sprite table",
               "the copied data does not match the source");
    }
    {
        // The register reads back the page that was written.
        Bench b; b.load({0x00});
        b.bus().poke(0xFF46, 0xC0);
        check_u8("0xFF46 reads back the source page", b.bus().peek(0xFF46), 0xC0);
    }
    {
        // Halfway through, only half the bytes have moved.
        Bench b; b.load({0x00});
        for (int i = 0; i < 160; ++i)
            b.bus().poke(static_cast<u16>(0xC000 + i), 0x5A);
        for (int i = 0; i < 160; ++i) b.bus().poke(static_cast<u16>(0xFE00 + i), 0x00);

        b.bus().poke(0xFF46, 0xC0);
        b.bus().tick(4 + 80 * 4);        // startup plus eighty bytes
        report(b.bus().dma().active(), "the transfer is still running halfway",
               "it finished too early");
        report(b.bus().peek(0xFE00) == 0x5A && b.bus().peek(0xFE9F) == 0x00,
               "it copies in order, from the start",
               "the bytes did not arrive in order");
    }
    {
        // While it runs, the CPU can only reach HRAM. This is why games copy
        // their DMA routine into HRAM and run it from there.
        Bench b; b.load({0x00});
        b.bus().poke(0xC500, 0x42);      // work RAM
        b.bus().poke(0xFF80, 0x37);      // HRAM
        b.bus().poke(0xFF46, 0xC0);      // start a transfer

        check_u8("during a transfer, work RAM reads 0xFF", b.bus().read(0xC500), 0xFF);
        check_u8("but HRAM still reads normally", b.bus().read(0xFF80), 0x37);

        b.bus().tick(160 * 4 + 8);
        check_u8("once it is over, work RAM reads normally again",
                 b.bus().read(0xC500), 0x42);
    }
    {
        // A second write restarts the copy from the beginning.
        Bench b; b.load({0x00});
        for (int i = 0; i < 160; ++i) b.bus().poke(static_cast<u16>(0xC000 + i), 0x11);
        for (int i = 0; i < 160; ++i) b.bus().poke(static_cast<u16>(0xD000 + i), 0x22);

        b.bus().poke(0xFF46, 0xC0);
        b.bus().tick(4 + 40 * 4);        // partway through
        b.bus().poke(0xFF46, 0xD0);      // restart from another page
        b.bus().tick(160 * 4 + 8);
        report(b.bus().peek(0xFE00) == 0x22 && b.bus().peek(0xFE9F) == 0x22,
               "a second write restarts the copy from the beginning",
               "the restart did not take effect over the whole table");
    }

    // === Rendering ==========================================================
    //  The console stores building blocks and a plan, never a picture. These
    //  checks drive one tile through the whole chain and then exercise the
    //  rules that decide what covers what.
    if (verbose) std::printf("\n== rendering ==\n");
    {
        // Helper: a bench with an identity palette and one known tile.
        auto make_bench = [](Bench &b) {
            b.load({0x00});
            b.bus().poke(0xFF47, 0xE4);     // BGP: colour n maps to shade n
            b.bus().poke(0xFF48, 0xE4);     // OBP0, likewise
            b.bus().poke(0xFF49, 0xE4);     // OBP1
            // Tile 1, first row. The two bits of one pixel come from two
            // different bytes, which is the part that trips everyone up.
            //   0x3C = 0 0 1 1 1 1 0 0   low bits
            //   0x7E = 0 1 1 1 1 1 1 0   high bits
            //   ->     0 2 3 3 3 3 2 0
            b.bus().poke(0x8010, 0x3C);
            b.bus().poke(0x8011, 0x7E);
            b.bus().poke(0x9800, 0x01);     // top-left map cell uses tile 1
        };
        auto pixel = [](Bench &b, int x, int y) {
            return b.bus().ppu().framebuffer()[static_cast<std::size_t>(y) * kScreenWidth + x];
        };

        {
            Bench b; make_bench(b);
            b.bus().tick(kTCyclesPerFrame);
            const u8 expected[8] = {0, 2, 3, 3, 3, 3, 2, 0};
            bool ok = true;
            for (int x = 0; x < 8; ++x)
                if (pixel(b, x, 0) != Ppu::dmg_shade(expected[x])) ok = false;
            report(ok, "a tile decodes to 0 2 3 3 3 3 2 0",
                   "the two-bits-in-two-bytes decoding is wrong");
        }
        {
            // The palette turns an index into a shade. Changing it repaints
            // the screen without touching a single pixel, which is how games
            // fade to black.
            Bench b; make_bench(b);
            b.bus().poke(0xFF47, 0x00);     // every index maps to shade 0
            b.bus().tick(kTCyclesPerFrame);
            bool ok = true;
            for (int x = 0; x < 8; ++x)
                if (pixel(b, x, 0) != Ppu::dmg_shade(0)) ok = false;
            report(ok, "the palette repaints without touching pixels",
                   "changing BGP did not change the image");
        }
        {
            // Scrolling moves the map under the screen.
            Bench b; make_bench(b);
            b.bus().poke(0xFF43, 0x01);     // SCX = 1
            b.bus().tick(kTCyclesPerFrame);
            const u8 expected[7] = {2, 3, 3, 3, 3, 2, 0};
            bool ok = true;
            for (int x = 0; x < 7; ++x)
                if (pixel(b, x, 0) != Ppu::dmg_shade(expected[x])) ok = false;
            report(ok, "SCX shifts the background", "scrolling did not shift the image");
        }
        {
            // Clearing LCDC bit 0 blanks the background entirely on a DMG.
            Bench b; make_bench(b);
            b.bus().poke(0xFF40, 0x90);     // screen on, background off
            b.bus().tick(kTCyclesPerFrame);
            report(pixel(b, 2, 0) == Ppu::dmg_shade(0),
                   "LCDC bit 0 blanks the background", "the background was still drawn");
        }
        {
            // The window is a second background layer that does not scroll.
            Bench b; make_bench(b);
            b.bus().poke(0x9C00, 0x01);     // window map, top-left cell
            b.bus().poke(0xFF4A, 0x00);     // WY = 0
            b.bus().poke(0xFF4B, 0x07);     // WX = 7 means x = 0
            b.bus().poke(0xFF40, 0xF1);     // window on, using the 0x9C00 map
            b.bus().tick(kTCyclesPerFrame);
            report(pixel(b, 1, 0) == Ppu::dmg_shade(2) && pixel(b, 2, 0) == Ppu::dmg_shade(3),
                   "the window draws over the background", "the window did not appear");
        }
        {
            // A sprite sits at Y-16, X-8: a sprite at (16, 8) lands on (0, 0).
            Bench b; make_bench(b);
            b.bus().poke(0xFF40, 0x93);     // sprites enabled
            b.bus().poke(0x9800, 0x00);     // clear the background behind it
            b.bus().poke(0xFE00, 16);       // Y
            b.bus().poke(0xFE01, 8);        // X
            b.bus().poke(0xFE02, 0x01);     // tile 1
            b.bus().poke(0xFE03, 0x00);     // no attributes
            b.bus().tick(kTCyclesPerFrame);
            report(pixel(b, 1, 0) == Ppu::dmg_shade(2) && pixel(b, 2, 0) == Ppu::dmg_shade(3),
                   "a sprite is placed at X-8, Y-16", "the sprite did not appear where expected");
            report(pixel(b, 0, 0) == Ppu::dmg_shade(0),
                   "sprite colour 0 is transparent", "colour 0 was drawn");
        }
        {
            // Horizontal flip mirrors the eight pixels.
            Bench b; make_bench(b);
            b.bus().poke(0xFF40, 0x93);
            b.bus().poke(0x9800, 0x00);
            b.bus().poke(0xFE00, 16); b.bus().poke(0xFE01, 8);
            b.bus().poke(0xFE02, 0x01); b.bus().poke(0xFE03, 0x20);   // X flip
            b.bus().tick(kTCyclesPerFrame);
            report(pixel(b, 6, 0) == Ppu::dmg_shade(2) && pixel(b, 5, 0) == Ppu::dmg_shade(3),
                   "a sprite can be flipped horizontally", "the flip had no effect");
        }
        {
            // Attribute bit 7 puts a sprite behind background colours 1 to 3,
            // but still in front of colour 0.
            Bench b; make_bench(b);
            b.bus().poke(0xFF40, 0x93);
            b.bus().poke(0xFE00, 16); b.bus().poke(0xFE01, 8);
            b.bus().poke(0xFE02, 0x01); b.bus().poke(0xFE03, 0x80);   // behind the background
            b.bus().tick(kTCyclesPerFrame);
            report(pixel(b, 2, 0) == Ppu::dmg_shade(3),
                   "a background-priority sprite hides behind colours 1 to 3",
                   "the sprite was drawn over the background");
        }
        {
            // Only TEN sprites fit on one line, chosen by their order in
            // memory rather than by position. This is why sprites flicker in
            // real games when too many crowd a line.
            Bench b; make_bench(b);
            b.bus().poke(0xFF40, 0x93);
            b.bus().poke(0x9800, 0x00);
            for (int i = 0; i < 11; ++i) {
                b.bus().poke(static_cast<u16>(0xFE00 + i * 4 + 0), 16);
                b.bus().poke(static_cast<u16>(0xFE00 + i * 4 + 1), static_cast<u8>(8 + i * 8));
                b.bus().poke(static_cast<u16>(0xFE00 + i * 4 + 2), 0x01);
                b.bus().poke(static_cast<u16>(0xFE00 + i * 4 + 3), 0x00);
            }
            b.bus().tick(kTCyclesPerFrame);
            report(pixel(b, 9 * 8 + 2, 0) == Ppu::dmg_shade(3),
                   "the tenth sprite on a line is drawn", "the tenth sprite was dropped");
            report(pixel(b, 10 * 8 + 2, 0) == Ppu::dmg_shade(0),
                   "the eleventh is not", "an eleventh sprite was drawn");
        }
        {
            // Among the ten, the one further left wins.
            Bench b; make_bench(b);
            b.bus().poke(0xFF40, 0x93);
            b.bus().poke(0x9800, 0x00);
            b.bus().poke(0x8020, 0xFF); b.bus().poke(0x8021, 0xFF);   // tile 2: solid colour 3
            // Sprite 0 is further right but earlier in memory; sprite 1 is
            // further left, so sprite 1 must win.
            b.bus().poke(0xFE00, 16); b.bus().poke(0xFE01, 9);
            b.bus().poke(0xFE02, 0x02); b.bus().poke(0xFE03, 0x00);   // solid
            b.bus().poke(0xFE04, 16); b.bus().poke(0xFE05, 8);
            b.bus().poke(0xFE06, 0x01); b.bus().poke(0xFE07, 0x00);   // the pattern
            b.bus().tick(kTCyclesPerFrame);
            report(pixel(b, 0, 0) == Ppu::dmg_shade(0),
                   "the leftmost sprite wins, and its transparent pixel stays transparent",
                   "the sprite further right showed through");
        }
        {
            // In 8x16 mode the low bit of the tile index is ignored.
            Bench b; make_bench(b);
            b.bus().poke(0xFF40, 0x97);     // sprites enabled, 8x16
            b.bus().poke(0x9800, 0x00);
            b.bus().poke(0x8030, 0xFF); b.bus().poke(0x8031, 0xFF);   // tile 3, first row
            b.bus().poke(0xFE00, 16); b.bus().poke(0xFE01, 8);
            b.bus().poke(0xFE02, 0x03);     // odd index: the hardware uses 2
            b.bus().poke(0xFE03, 0x00);
            b.bus().tick(kTCyclesPerFrame);
            report(pixel(b, 2, 0) != Ppu::dmg_shade(3),
                   "an 8x16 sprite ignores bit 0 of its tile index",
                   "the odd tile index was used as is");
        }
    }

    // === The disassembler must agree with the CPU on every opcode ==========
    //  Section V.1 of the subject requires the debugger to display the next
    //  instruction. That display is only trustworthy if the disassembler's
    //  idea of an instruction's length matches what the CPU consumes: one
    //  byte of disagreement and every following line of a listing is wrong.
    //
    //  Every opcode is run on a fresh bench and PC's advance is compared with
    //  the reported length. Conditional branches are set up so the branch is
    //  NOT taken, which makes them advance sequentially like everything else.
    if (verbose) std::printf("\n== disassembler vs CPU, all 512 opcodes ==\n");
    {
        // The unconditional jumps never advance sequentially, so they are
        // checked separately just below.
        auto is_unconditional_jump = [](u8 op) {
            switch (op) {
                case 0x18: case 0xC3: case 0xC9: case 0xCD: case 0xD9: case 0xE9:
                case 0xC7: case 0xCF: case 0xD7: case 0xDF:
                case 0xE7: case 0xEF: case 0xF7: case 0xFF:
                    return true;
                default:
                    return false;
            }
        };

        // Flags that make a conditional branch fall through: NZ and NC need
        // their flag set, Z and C need it clear.
        // The mask must be 0xE7, not 0xC7: bit 5 belongs to the condition field,
        // so it has to be cleared before comparing. With 0xC7 the JR line was
        // dead code (bit 5 is not in that mask, so the test could never be
        // true) and the three others also matched LDH and LD (nn),A by
        // accident. Clang's -Wtautological-bitwise-compare caught it; GCC did
        // not warn.
        auto flags_that_avoid_the_branch = [](u8 op) -> u8 {
            const bool conditional = (op & 0xE7) == 0x20 ||   // JR cc
                                     (op & 0xE7) == 0xC0 ||   // RET cc
                                     (op & 0xE7) == 0xC2 ||   // JP cc
                                     (op & 0xE7) == 0xC4;     // CALL cc
            if (!conditional) return 0;
            const int cc = (op >> 3) & 0x03;
            return (cc == 0 || cc == 2) ? static_cast<u8>(FlagZ | FlagC) : 0;
        };

        int mismatches = 0;
        int covered    = 0;

        for (int prefixed = 0; prefixed < 2; ++prefixed) {
            for (int op = 0; op < 256; ++op) {
                const u8 opcode = static_cast<u8>(op);
                if (!prefixed && is_unconditional_jump(opcode)) continue;
                if (!prefixed && opcode == 0xCB) continue;   // covered by the CB pass

                // The operand bytes are deliberately NOT zero. With 0x00 a
                // taken relative jump lands on pc + 2, exactly where a
                // fall-through lands, so a mistake in the branch setup would
                // stay invisible. 0x10/0x20 makes the two outcomes differ.
                Bench b;
                const std::vector<u8> code =
                    prefixed ? std::vector<u8>{0xCB, opcode, 0x10, 0x20}
                             : std::vector<u8>{opcode, 0x10, 0x20, 0x00};
                if (!b.load(code)) continue;

                b.cpu().regs().set_hl(0xC000);   // keep (HL) on writable memory
                b.cpu().regs().sp = 0xC100;
                b.cpu().regs().f  = flags_that_avoid_the_branch(opcode);

                const u16 pc_before = b.cpu().regs().pc;
                const Instruction ins = disassemble(b.bus(), pc_before);
                b.step();
                ++covered;

                const u16 expected = static_cast<u16>(pc_before + ins.length);
                if (b.cpu().regs().pc != expected && mismatches < 8) {
                    char detail[192];
                    std::snprintf(detail, sizeof(detail),
                                  "%s$%02X \"%s\": disassembler says %u byte(s), "
                                  "PC went from $%04X to $%04X",
                                  prefixed ? "CB " : "", opcode, ins.text.c_str(),
                                  ins.length, pc_before, b.cpu().regs().pc);
                    report(false, "opcode length", detail);
                    ++mismatches;
                }
            }
        }

        char detail[96];
        std::snprintf(detail, sizeof(detail), "%d mismatch(es) over %d opcodes",
                      mismatches, covered);
        report(mismatches == 0, "497 sequential opcodes have the right length", detail);
    }

    // What the disassembler actually prints. Length alone is not enough: the
    // debugger's listing is what a human reads while hunting a bug, so the
    // operand formatting has to be right too.
    if (verbose) std::printf("\n== disassembler output ==\n");
    {
        struct TextCase { std::vector<u8> code; const char *expected; };
        const TextCase text_cases[] = {
            {{0x00},                   "NOP"},
            {{0x42},                   "LD B, D"},
            {{0x76},                   "HALT"},
            {{0xC9},                   "RET"},
            {{0x3E, 0x42},             "LD A, $42"},
            {{0x01, 0x34, 0x12},       "LD BC, $1234"},
            {{0xC3, 0x50, 0x01},       "JP $0150"},
            {{0x18, 0xFE},             "JR $0100"},          // the classic self-loop
            {{0x20, 0x05},             "JR NZ, $0107"},      // target, not offset
            {{0xCD, 0x00, 0xC0},       "CALL $C000"},
            {{0x08, 0x00, 0xC0},       "LD ($C000), SP"},
            {{0xE0, 0x44},             "LDH ($44), A"},
            {{0xF0, 0x44},             "LDH A, ($44)"},
            {{0xE2},                   "LDH ($FF00+C), A"},
            {{0x22},                   "LD (HL+), A"},
            {{0x3A},                   "LD A, (HL-)"},
            {{0x86},                   "ADD A, (HL)"},
            {{0xFE, 0x90},             "CP $90"},
            {{0xE8, 0xFB},             "ADD SP, -5"},        // signed operand
            {{0xF8, 0x05},             "LD HL, SP+5"},
            {{0xDF},                   "RST $18"},
            {{0xCB, 0x40},             "BIT 0, B"},
            {{0xCB, 0x7E},             "BIT 7, (HL)"},
            {{0xCB, 0x86},             "RES 0, (HL)"},
            {{0xCB, 0x30},             "SWAP B"},
            {{0xD3},                   "<illegal $D3>"},
        };
        for (const TextCase &c : text_cases) {
            Bench b;
            if (!b.load(c.code)) continue;
            check_text(c.expected, disassemble(b.bus(), 0x0100).text, c.expected);
        }
    }

    // Inspecting must not perturb: the disassembler reads through peek, which
    // does not advance the clock (decision D15).
    {
        Bench b;
        b.load({0xC3, 0x50, 0x01});
        const u64 before = b.bus().clock().t_cpu();
        for (int i = 0; i < 500; ++i) (void)disassemble(b.bus(), 0x0100);
        report(b.bus().clock().t_cpu() == before,
               "disassembling 500 times costs no cycles",
               "the clock moved while inspecting");
    }

    // The fourteen unconditional jumps: their length is confirmed by where
    // they land, or by the return address they push.
    {
        Bench b; b.load({0x18, 0x05});              // JR +5 at 0x0100
        b.step();
        check_u16("JR d lands at pc + 2 + offset", b.cpu().regs().pc, 0x0107);
    }
    {
        Bench b; b.load({0xC3, 0x34, 0x12});        // JP $1234
        b.step();
        check_u16("JP nn reads a 16-bit operand", b.cpu().regs().pc, 0x1234);
    }
    {
        Bench b; b.load({0xCD, 0x34, 0x12});        // CALL $1234
        b.cpu().regs().sp = 0xC002;
        b.step();
        check_u16("CALL nn pushes pc + 3",
                  static_cast<u16>((b.bus().peek(0xC001) << 8) | b.bus().peek(0xC000)), 0x0103);
    }
    {
        // RET, RETI, JP HL and the eight RSTs carry no operand, so their
        // length is simply asserted to be one byte.
        const u8 one_byte_jumps[] = {0xC9, 0xD9, 0xE9,
                                     0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF};
        Bench b; b.load({0x00});
        bool all_one = true;
        for (u8 op : one_byte_jumps) {
            b.bus().poke(0x0100, op);
            if (disassemble(b.bus(), 0x0100).length != 1) all_one = false;
        }
        report(all_one, "RET, RETI, JP HL and RST are one byte long",
               "one of them was reported with the wrong length");
    }

    std::printf("\n");
    if (failures == 0) std::printf("\033[1;32m%d checks passed, 0 failed\033[0m\n", checks);
    else               std::printf("\033[1;31m%d checks passed, %d failed\033[0m\n",
                                   checks - failures, failures);
    return failures;
}

}  // namespace retroemu
