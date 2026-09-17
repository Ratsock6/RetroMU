#include "retroemu/core/cpu.hpp"

namespace retroemu {
namespace {

// ALU operation indices, as laid out in the opcode map.
enum AluOp { AluAdd, AluAdc, AluSub, AluSbc, AluAnd, AluXor, AluOr, AluCp };

// Rotate/shift indices, used by 0x07/0x0F/0x17/0x1F and the whole CB page.
enum RotOp { RotRlc, RotRrc, RotRl, RotRr, RotSla, RotSra, RotSwap, RotSrl };

constexpr u32 kInternalCycle = 4;   // one M-cycle spent without touching memory

}  // namespace

// ---------------------------------------------------------------------------
//  Reset
// ---------------------------------------------------------------------------
void Cpu::reset(Model model)
{
    // Values the real boot ROM leaves in the registers just before jumping to
    // 0x0100. Hard-coding them is what lets the mandatory part skip the boot
    // sequence entirely; reproducing that sequence is a bonus (subject Ch. VI).
    if (model == Model::Cgb) {
        regs_.set_af(0x1180);
        regs_.set_bc(0x0000);
        regs_.set_de(0xFF56);
        regs_.set_hl(0x000D);
    } else {
        regs_.set_af(0x01B0);
        regs_.set_bc(0x0013);
        regs_.set_de(0x00D8);
        regs_.set_hl(0x014D);
    }
    regs_.sp = 0xFFFE;
    regs_.pc = 0x0100;

    ime_         = false;
    ime_pending_ = false;
    halted_      = false;
    halt_bug_    = false;
    stopped_     = false;
    illegal_     = false;
    illegal_opcode_ = 0;
}

// ---------------------------------------------------------------------------
//  One step
// ---------------------------------------------------------------------------
u32 Cpu::step(Bus &bus)
{
    const u64 start = bus.clock().t_cpu();

    // An interrupt is checked before anything else, and also wakes the CPU
    // from HALT even when interrupts are globally disabled.
    if (service_interrupt(bus)) {
        return static_cast<u32>(bus.clock().t_cpu() - start);
    }

    if (halted_ || stopped_ || illegal_) {
        bus.tick(kInternalCycle);   // the clock keeps running; the CPU does not
        return static_cast<u32>(bus.clock().t_cpu() - start);
    }

    // EI does not take effect immediately: interrupts become enabled only
    // after the instruction FOLLOWING it. Capturing the flag here, before the
    // fetch, is what implements that one-instruction delay.
    const bool enable_ime_after = ime_pending_;

    const u8 opcode = fetch8(bus);
    execute(bus, opcode);

    if (enable_ime_after) {
        ime_         = true;
        ime_pending_ = false;
    }

    return static_cast<u32>(bus.clock().t_cpu() - start);
}

// ---------------------------------------------------------------------------
//  Interrupts
// ---------------------------------------------------------------------------
bool Cpu::service_interrupt(Bus &bus)
{
    const u8 pending = static_cast<u8>(bus.interrupt_enable() & bus.interrupt_flags() & 0x1F);
    if (pending == 0) return false;

    // A pending interrupt wakes the CPU whether or not IME is set. That is why
    // this test comes before the IME check.
    halted_ = false;
    if (!ime_) return false;

    ime_ = false;

    // Dispatch costs 5 M-cycles: two internal, two for pushing PC, one more
    // internal while the new PC is latched.
    bus.tick(kInternalCycle * 2);
    push16(bus, regs_.pc);

    // Lowest bit wins: VBlank, then STAT, Timer, Serial, Joypad.
    for (int i = 0; i < 5; ++i) {
        if (pending & (1 << i)) {
            bus.set_interrupt_flags(static_cast<u8>(bus.interrupt_flags() & ~(1 << i)));
            regs_.pc = static_cast<u16>(0x40 + i * 8);
            break;
        }
    }
    bus.tick(kInternalCycle);
    return true;
}

// ---------------------------------------------------------------------------
//  Fetching
// ---------------------------------------------------------------------------
u8 Cpu::fetch8(Bus &bus)
{
    const u8 value = bus.read(regs_.pc);
    if (halt_bug_) {
        // Hardware quirk: when HALT is executed with IME clear while an
        // interrupt is already pending, the CPU does not halt and the byte
        // after HALT is read twice, because PC fails to increment once.
        halt_bug_ = false;
    } else {
        ++regs_.pc;
    }
    return value;
}

u16 Cpu::fetch16(Bus &bus)
{
    const u8 low  = fetch8(bus);
    const u8 high = fetch8(bus);
    return static_cast<u16>((high << 8) | low);   // little-endian
}

// ---------------------------------------------------------------------------
//  Operand helpers
// ---------------------------------------------------------------------------
u8 Cpu::read_r8(Bus &bus, int index)
{
    switch (index) {
        case 0: return regs_.b;
        case 1: return regs_.c;
        case 2: return regs_.d;
        case 3: return regs_.e;
        case 4: return regs_.h;
        case 5: return regs_.l;
        case 6: return bus.read(regs_.hl());   // costs 4 cycles
        default: return regs_.a;
    }
}

void Cpu::write_r8(Bus &bus, int index, u8 value)
{
    switch (index) {
        case 0: regs_.b = value; return;
        case 1: regs_.c = value; return;
        case 2: regs_.d = value; return;
        case 3: regs_.e = value; return;
        case 4: regs_.h = value; return;
        case 5: regs_.l = value; return;
        case 6: bus.write(regs_.hl(), value); return;
        default: regs_.a = value; return;
    }
}

u16 Cpu::read_rp(int index) const
{
    switch (index) {
        case 0: return regs_.bc();
        case 1: return regs_.de();
        case 2: return regs_.hl();
        default: return regs_.sp;
    }
}

void Cpu::write_rp(int index, u16 value)
{
    switch (index) {
        case 0: regs_.set_bc(value); return;
        case 1: regs_.set_de(value); return;
        case 2: regs_.set_hl(value); return;
        default: regs_.sp = value; return;
    }
}

u16 Cpu::read_rp2(int index) const
{
    return index == 3 ? regs_.af() : read_rp(index);
}

void Cpu::write_rp2(int index, u16 value)
{
    if (index == 3) regs_.set_af(value);   // masks the low nibble of F
    else            write_rp(index, value);
}

bool Cpu::condition(int index) const
{
    switch (index) {
        case 0: return !regs_.flag(FlagZ);   // NZ
        case 1: return  regs_.flag(FlagZ);   // Z
        case 2: return !regs_.flag(FlagC);   // NC
        default: return regs_.flag(FlagC);   // C
    }
}

// ---------------------------------------------------------------------------
//  Stack. It grows downwards, and the high byte is pushed first.
// ---------------------------------------------------------------------------
void Cpu::push16(Bus &bus, u16 value)
{
    regs_.sp = static_cast<u16>(regs_.sp - 1);
    bus.write(regs_.sp, static_cast<u8>(value >> 8));
    regs_.sp = static_cast<u16>(regs_.sp - 1);
    bus.write(regs_.sp, static_cast<u8>(value & 0xFF));
}

u16 Cpu::pop16(Bus &bus)
{
    const u8 low = bus.read(regs_.sp);
    regs_.sp = static_cast<u16>(regs_.sp + 1);
    const u8 high = bus.read(regs_.sp);
    regs_.sp = static_cast<u16>(regs_.sp + 1);
    return static_cast<u16>((high << 8) | low);
}

// ---------------------------------------------------------------------------
//  Arithmetic
// ---------------------------------------------------------------------------
//  The half-carry is the single most common source of bugs in this project.
//  It means "did a carry cross from bit 3 into bit 4", so it is computed by
//  adding only the low nibbles and checking whether that overflows.
// ---------------------------------------------------------------------------
void Cpu::alu(int operation, u8 value)
{
    const u8 a     = regs_.a;
    const u8 carry = regs_.flag(FlagC) ? 1 : 0;

    switch (operation) {
        case AluAdd: {
            const u16 result = static_cast<u16>(a + value);
            regs_.a = static_cast<u8>(result);
            regs_.set_flags(regs_.a == 0, false,
                            ((a & 0x0F) + (value & 0x0F)) > 0x0F,
                            result > 0xFF);
            return;
        }
        case AluAdc: {
            const u16 result = static_cast<u16>(a + value + carry);
            regs_.a = static_cast<u8>(result);
            regs_.set_flags(regs_.a == 0, false,
                            ((a & 0x0F) + (value & 0x0F) + carry) > 0x0F,
                            result > 0xFF);
            return;
        }
        case AluSub: {
            const u8 result = static_cast<u8>(a - value);
            regs_.a = result;
            regs_.set_flags(result == 0, true,
                            (a & 0x0F) < (value & 0x0F),
                            a < value);
            return;
        }
        case AluSbc: {
            const int result = a - value - carry;
            regs_.a = static_cast<u8>(result);
            regs_.set_flags(regs_.a == 0, true,
                            (a & 0x0F) < ((value & 0x0F) + carry),
                            result < 0);
            return;
        }
        case AluAnd:
            regs_.a = static_cast<u8>(a & value);
            regs_.set_flags(regs_.a == 0, false, true, false);   // H is set, oddly
            return;
        case AluXor:
            regs_.a = static_cast<u8>(a ^ value);
            regs_.set_flags(regs_.a == 0, false, false, false);
            return;
        case AluOr:
            regs_.a = static_cast<u8>(a | value);
            regs_.set_flags(regs_.a == 0, false, false, false);
            return;
        default: {   // CP: a subtraction whose result is thrown away
            const u8 result = static_cast<u8>(a - value);
            regs_.set_flags(result == 0, true,
                            (a & 0x0F) < (value & 0x0F),
                            a < value);
            return;
        }
    }
}

// INC and DEC on 8-bit operands leave the carry flag untouched.
u8 Cpu::inc8(u8 value)
{
    const u8 result = static_cast<u8>(value + 1);
    regs_.set_flag(FlagZ, result == 0);
    regs_.set_flag(FlagN, false);
    regs_.set_flag(FlagH, (value & 0x0F) == 0x0F);
    return result;
}

u8 Cpu::dec8(u8 value)
{
    const u8 result = static_cast<u8>(value - 1);
    regs_.set_flag(FlagZ, result == 0);
    regs_.set_flag(FlagN, true);
    regs_.set_flag(FlagH, (value & 0x0F) == 0x00);
    return result;
}

// ADD HL,rr works on 16 bits, so the half-carry moves to bit 11 -> 12.
// The zero flag is NOT affected.
void Cpu::add_hl(Bus &bus, u16 value)
{
    const u16 hl     = regs_.hl();
    const u32 result = static_cast<u32>(hl) + value;
    regs_.set_flag(FlagN, false);
    regs_.set_flag(FlagH, ((hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF);
    regs_.set_flag(FlagC, result > 0xFFFF);
    regs_.set_hl(static_cast<u16>(result));
    bus.tick(kInternalCycle);
}

// ADD SP,e8 and LD HL,SP+e8 share this. Counter-intuitively, their flags are
// computed on the LOW BYTE of SP, as if it were an 8-bit addition.
u16 Cpu::add_sp_signed(Bus &bus, i8 offset)
{
    const u16 sp      = regs_.sp;
    const u8  operand = static_cast<u8>(offset);
    regs_.set_flags(false, false,
                    ((sp & 0x0F) + (operand & 0x0F)) > 0x0F,
                    ((sp & 0xFF) + operand) > 0xFF);
    bus.tick(kInternalCycle);
    return static_cast<u16>(sp + static_cast<i16>(offset));
}

// DAA fixes up A after an addition or subtraction of packed decimal digits.
// It is the only instruction that reads the N and H flags, which is precisely
// why getting the half-carry right everywhere else matters.
void Cpu::daa()
{
    u8   a     = regs_.a;
    bool carry = regs_.flag(FlagC);

    if (!regs_.flag(FlagN)) {
        if (carry || a > 0x99)                      { a = static_cast<u8>(a + 0x60); carry = true; }
        if (regs_.flag(FlagH) || (a & 0x0F) > 0x09) { a = static_cast<u8>(a + 0x06); }
    } else {
        if (carry)             a = static_cast<u8>(a - 0x60);
        if (regs_.flag(FlagH)) a = static_cast<u8>(a - 0x06);
    }

    regs_.a = a;
    regs_.set_flag(FlagZ, a == 0);
    regs_.set_flag(FlagH, false);
    regs_.set_flag(FlagC, carry);
}

// Shared by the four one-byte rotates and the whole CB page. The difference:
// RLCA/RRCA/RLA/RRA always clear Z, while their CB counterparts set it from
// the result.
u8 Cpu::rotate(int operation, u8 value, bool cb_form)
{
    const bool old_carry = regs_.flag(FlagC);
    u8   result = 0;
    bool carry  = false;

    switch (operation) {
        case RotRlc:  carry = (value & 0x80) != 0;
                      result = static_cast<u8>((value << 1) | (carry ? 1 : 0)); break;
        case RotRrc:  carry = (value & 0x01) != 0;
                      result = static_cast<u8>((value >> 1) | (carry ? 0x80 : 0)); break;
        case RotRl:   carry = (value & 0x80) != 0;
                      result = static_cast<u8>((value << 1) | (old_carry ? 1 : 0)); break;
        case RotRr:   carry = (value & 0x01) != 0;
                      result = static_cast<u8>((value >> 1) | (old_carry ? 0x80 : 0)); break;
        case RotSla:  carry = (value & 0x80) != 0;
                      result = static_cast<u8>(value << 1); break;
        case RotSra:  carry = (value & 0x01) != 0;   // arithmetic: bit 7 is kept
                      result = static_cast<u8>((value >> 1) | (value & 0x80)); break;
        case RotSwap: result = static_cast<u8>((value << 4) | (value >> 4)); break;
        default:      carry = (value & 0x01) != 0;   // SRL: logical, bit 7 becomes 0
                      result = static_cast<u8>(value >> 1); break;
    }

    regs_.set_flags(cb_form ? (result == 0) : false, false, false, carry);
    return result;
}

// ---------------------------------------------------------------------------
//  The main decoder
// ---------------------------------------------------------------------------
void Cpu::execute(Bus &bus, u8 opcode)
{
    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 0x07;
    const int z = opcode & 0x07;
    const int p = y >> 1;
    const int q = y & 0x01;

    switch (x) {

    // === 0x00-0x3F : assorted ==============================================
    case 0:
        switch (z) {
            case 0:
                if (y == 0) return;                                  // NOP
                if (y == 1) {                                        // LD (nn),SP
                    const u16 addr = fetch16(bus);
                    bus.write(addr, static_cast<u8>(regs_.sp & 0xFF));
                    bus.write(static_cast<u16>(addr + 1), static_cast<u8>(regs_.sp >> 8));
                    return;
                }
                if (y == 2) {                                        // STOP
                    fetch8(bus);          // STOP is followed by a padding byte

                    // On a CGB, STOP has a second job. If the game has armed
                    // the speed switch by writing to KEY1, the console does
                    // NOT stop: it changes the CPU clock and carries on. A
                    // game that uses double speed executes STOP on purpose,
                    // and treating it as a halt would freeze it on its first
                    // frame.
                    if (bus.speed_switch_armed()) {
                        bus.perform_speed_switch();
                        return;
                    }
                    stopped_ = true;
                    return;
                }
                if (y == 3) {                                        // JR d
                    const i8 offset = static_cast<i8>(fetch8(bus));
                    regs_.pc = static_cast<u16>(regs_.pc + offset);
                    bus.tick(kInternalCycle);
                    return;
                }
                {                                                    // JR cc,d
                    const i8 offset = static_cast<i8>(fetch8(bus));
                    if (condition(y - 4)) {
                        regs_.pc = static_cast<u16>(regs_.pc + offset);
                        bus.tick(kInternalCycle);   // only when the branch is taken
                    }
                    return;
                }

            case 1:
                if (q == 0) { write_rp(p, fetch16(bus)); return; }    // LD rp,nn
                add_hl(bus, read_rp(p));                             // ADD HL,rp
                return;

            case 2:
                switch (p) {
                    case 0: if (q == 0) bus.write(regs_.bc(), regs_.a);
                            else        regs_.a = bus.read(regs_.bc());
                            return;
                    case 1: if (q == 0) bus.write(regs_.de(), regs_.a);
                            else        regs_.a = bus.read(regs_.de());
                            return;
                    case 2: {                                        // (HL+)
                        const u16 hl = regs_.hl();
                        if (q == 0) bus.write(hl, regs_.a);
                        else        regs_.a = bus.read(hl);
                        regs_.set_hl(static_cast<u16>(hl + 1));
                        return;
                    }
                    default: {                                       // (HL-)
                        const u16 hl = regs_.hl();
                        if (q == 0) bus.write(hl, regs_.a);
                        else        regs_.a = bus.read(hl);
                        regs_.set_hl(static_cast<u16>(hl - 1));
                        return;
                    }
                }

            case 3:                                                  // INC/DEC rp
                write_rp(p, static_cast<u16>(read_rp(p) + (q == 0 ? 1 : -1)));
                bus.tick(kInternalCycle);
                return;

            case 4: write_r8(bus, y, inc8(read_r8(bus, y))); return; // INC r
            case 5: write_r8(bus, y, dec8(read_r8(bus, y))); return; // DEC r
            case 6: write_r8(bus, y, fetch8(bus)); return;           // LD r,n

            default:
                switch (y) {
                    case 0: regs_.a = rotate(RotRlc, regs_.a, false); return;  // RLCA
                    case 1: regs_.a = rotate(RotRrc, regs_.a, false); return;  // RRCA
                    case 2: regs_.a = rotate(RotRl,  regs_.a, false); return;  // RLA
                    case 3: regs_.a = rotate(RotRr,  regs_.a, false); return;  // RRA
                    case 4: daa(); return;
                    case 5: regs_.a = static_cast<u8>(~regs_.a);               // CPL
                            regs_.set_flag(FlagN, true);
                            regs_.set_flag(FlagH, true);
                            return;
                    case 6: regs_.set_flag(FlagN, false);                      // SCF
                            regs_.set_flag(FlagH, false);
                            regs_.set_flag(FlagC, true);
                            return;
                    default: regs_.set_flag(FlagN, false);                     // CCF
                             regs_.set_flag(FlagH, false);
                             regs_.set_flag(FlagC, !regs_.flag(FlagC));
                             return;
                }
        }

    // === 0x40-0x7F : LD r,r' (and HALT in the one hole) =====================
    case 1:
        if (y == 6 && z == 6) {
            // HALT. If interrupts are globally disabled but one is already
            // pending, the CPU does not stop; instead the next byte is read
            // twice. That is the documented "halt bug".
            const bool pending = (bus.interrupt_enable() & bus.interrupt_flags() & 0x1F) != 0;
            if (!ime_ && pending) halt_bug_ = true;
            else                  halted_   = true;
            return;
        }
        write_r8(bus, y, read_r8(bus, z));
        return;

    // === 0x80-0xBF : ALU on a register =====================================
    case 2:
        alu(y, read_r8(bus, z));
        return;

    // === 0xC0-0xFF : jumps, stack, I/O, immediates =========================
    default:
        switch (z) {
            case 0:
                if (y < 4) {                                         // RET cc
                    bus.tick(kInternalCycle);
                    if (condition(y)) { regs_.pc = pop16(bus); bus.tick(kInternalCycle); }
                    return;
                }
                if (y == 4) { bus.write(static_cast<u16>(0xFF00 + fetch8(bus)), regs_.a); return; }
                if (y == 6) { regs_.a = bus.read(static_cast<u16>(0xFF00 + fetch8(bus))); return; }
                if (y == 5) {                                        // ADD SP,e8
                    const i8 offset = static_cast<i8>(fetch8(bus));
                    regs_.sp = add_sp_signed(bus, offset);
                    bus.tick(kInternalCycle);   // ADD SP costs one cycle more than LD HL,SP+e
                    return;
                }
                {                                                    // LD HL,SP+e8
                    const i8 offset = static_cast<i8>(fetch8(bus));
                    regs_.set_hl(add_sp_signed(bus, offset));
                    return;
                }

            case 1:
                if (q == 0) { write_rp2(p, pop16(bus)); return; }     // POP rp2
                switch (p) {
                    case 0: regs_.pc = pop16(bus); bus.tick(kInternalCycle); return;  // RET
                    case 1: regs_.pc = pop16(bus); ime_ = true;                       // RETI
                            bus.tick(kInternalCycle); return;
                    case 2: regs_.pc = regs_.hl(); return;                            // JP HL
                    default: regs_.sp = regs_.hl(); bus.tick(kInternalCycle); return; // LD SP,HL
                }

            case 2:
                if (y < 4) {                                         // JP cc,nn
                    const u16 target = fetch16(bus);
                    if (condition(y)) { regs_.pc = target; bus.tick(kInternalCycle); }
                    return;
                }
                switch (y) {
                    case 4: bus.write(static_cast<u16>(0xFF00 + regs_.c), regs_.a); return;
                    case 5: bus.write(fetch16(bus), regs_.a); return;
                    case 6: regs_.a = bus.read(static_cast<u16>(0xFF00 + regs_.c)); return;
                    default: regs_.a = bus.read(fetch16(bus)); return;
                }

            case 3:
                if (y == 0) { regs_.pc = fetch16(bus); bus.tick(kInternalCycle); return; }  // JP nn
                if (y == 1) { execute_cb(bus, fetch8(bus)); return; }                       // CB prefix
                if (y == 6) { ime_ = false; ime_pending_ = false; return; }                 // DI
                if (y == 7) { ime_pending_ = true; return; }                                // EI
                illegal_ = true; illegal_opcode_ = opcode; return;

            case 4:
                if (y < 4) {                                         // CALL cc,nn
                    const u16 target = fetch16(bus);
                    if (condition(y)) {
                        bus.tick(kInternalCycle);
                        push16(bus, regs_.pc);
                        regs_.pc = target;
                    }
                    return;
                }
                illegal_ = true; illegal_opcode_ = opcode; return;

            case 5:
                if (q == 0) {                                        // PUSH rp2
                    bus.tick(kInternalCycle);
                    push16(bus, read_rp2(p));
                    return;
                }
                if (p == 0) {                                        // CALL nn
                    const u16 target = fetch16(bus);
                    bus.tick(kInternalCycle);
                    push16(bus, regs_.pc);
                    regs_.pc = target;
                    return;
                }
                illegal_ = true; illegal_opcode_ = opcode; return;

            case 6: alu(y, fetch8(bus)); return;                     // ALU with immediate

            default:                                                 // RST
                bus.tick(kInternalCycle);
                push16(bus, regs_.pc);
                regs_.pc = static_cast<u16>(y * 8);
                return;
        }
    }
}

// ---------------------------------------------------------------------------
//  The 0xCB page: rotates, shifts and single-bit operations
// ---------------------------------------------------------------------------
void Cpu::execute_cb(Bus &bus, u8 opcode)
{
    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 0x07;
    const int z = opcode & 0x07;

    if (x == 0) {                                   // rotate / shift / swap
        write_r8(bus, z, rotate(y, read_r8(bus, z), true));
        return;
    }

    const u8 value = read_r8(bus, z);

    if (x == 1) {                                   // BIT b,r: test only
        regs_.set_flag(FlagZ, (value & (1 << y)) == 0);
        regs_.set_flag(FlagN, false);
        regs_.set_flag(FlagH, true);
        return;                                     // carry is left untouched
    }

    if (x == 2) {                                   // RES b,r
        write_r8(bus, z, static_cast<u8>(value & ~(1 << y)));
        return;
    }

    write_r8(bus, z, static_cast<u8>(value | (1 << y)));   // SET b,r
}

}  // namespace retroemu
