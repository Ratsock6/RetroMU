#include "retroemu/debug/debugger.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "retroemu/debug/disassembler.hpp"

namespace retroemu {
namespace {

// ---------------------------------------------------------------------------
//  Display
// ---------------------------------------------------------------------------
void print_registers(const GameBoy &gb)
{
    const Registers &r = gb.cpu().regs();

    std::printf("  AF %04X   BC %04X   DE %04X   HL %04X\n",
                r.af(), r.bc(), r.de(), r.hl());
    std::printf("  SP %04X   PC %04X\n", r.sp, r.pc);
    std::printf("  flags %c%c%c%c   (Z N H C)\n",
                r.flag(FlagZ) ? 'Z' : '-', r.flag(FlagN) ? 'N' : '-',
                r.flag(FlagH) ? 'H' : '-', r.flag(FlagC) ? 'C' : '-');
    std::printf("  IME %s   IE %02X   IF %02X%s%s\n",
                gb.cpu().ime() ? "on " : "off",
                gb.bus().interrupt_enable(),
                static_cast<u8>(gb.bus().interrupt_flags() & 0x1F),
                gb.cpu().halted()  ? "   [halted]"  : "",
                gb.cpu().stopped() ? "   [stopped]" : "");
}

// The instruction at PC is what section V.1 calls "the next instruction to
// execute", so it is marked out from the ones after it.
void print_disassembly(const GameBoy &gb, u16 address, int count)
{
    for (int i = 0; i < count; ++i) {
        const Instruction ins = disassemble(gb.bus(), address);
        const bool at_pc = (address == gb.cpu().regs().pc);
        std::printf("  %s $%04X   %-9s  %s\n",
                    at_pc ? "->" : "  ", ins.address, ins.hex().c_str(), ins.text.c_str());
        address = static_cast<u16>(address + ins.length);
    }
}

void print_memory(const GameBoy &gb, u16 address, int count)
{
    for (int row = 0; row < (count + 15) / 16; ++row) {
        const u16 base = static_cast<u16>(address + row * 16);
        std::printf("  $%04X  ", base);

        for (int i = 0; i < 16; ++i)
            std::printf("%02X ", gb.bus().peek(static_cast<u16>(base + i)));

        std::printf(" ");
        for (int i = 0; i < 16; ++i) {
            const u8 c = gb.bus().peek(static_cast<u16>(base + i));
            std::printf("%c", (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.');
        }
        std::printf("\n");
    }
}

void print_state(const GameBoy &gb)
{
    const Clock &clk = gb.bus().clock();
    std::printf("  model        %s\n", gb.bus().model_name());
    std::printf("  t_cpu        %llu\n", static_cast<unsigned long long>(clk.t_cpu()));
    std::printf("  t_sys        %llu%s\n", static_cast<unsigned long long>(clk.t_sys()),
                clk.double_speed() ? "   (double speed: the CPU runs twice as fast)" : "");
    std::printf("  emulated     %.4f s\n",
                static_cast<double>(clk.t_sys()) / kSystemClockHz);
    std::printf("  frames drawn %llu\n",
                static_cast<unsigned long long>(gb.bus().ppu().frames()));
    std::printf("  screen       %s, LY %u, mode %s\n",
                gb.bus().ppu().lcd_on() ? "on" : "off",
                gb.bus().ppu().ly(), to_string(gb.bus().ppu().mode()));
    std::printf("  bus accesses %llu\n", static_cast<unsigned long long>(gb.bus().access_count()));
    std::printf("  cartridge    %s\n", gb.bus().cartridge().path().c_str());

    const std::string &serial = gb.bus().serial_output();
    if (!serial.empty())
        std::printf("  link port    \"%s\"\n", serial.c_str());
}

void print_help()
{
    std::printf(
        "  r                 registers, flags and interrupt state\n"
        "  d [addr] [n]      disassemble n instructions (default: from PC, 8)\n"
        "  s [n]             step n instructions (default 1)\n"
        "  f [n]             run n frames (default 1)\n"
        "  t [n]             run n seconds of emulation (default 1)\n"
        "  c                 run until a breakpoint is hit\n"
        "  b <addr>          set a breakpoint\n"
        "  bl                list breakpoints\n"
        "  bd <addr>         delete a breakpoint\n"
        "  m <addr> [n]      dump n bytes of memory (default 64)\n"
        "  w <addr> <value>  write a byte without advancing the clock\n"
        "  i                 machine state: clocks, frames, link port\n"
        "  reset             reset the machine\n"
        "  h                 this help\n"
        "  q                 quit\n");
}

// ---------------------------------------------------------------------------
//  Parsing. Numbers accept $C000, 0xC000 or plain decimal.
// ---------------------------------------------------------------------------
bool parse_number(const std::string &text, u32 &out)
{
    if (text.empty()) return false;

    int         base = 10;
    std::size_t start = 0;
    if (text[0] == '$')                         { base = 16; start = 1; }
    else if (text.size() > 2 && text[0] == '0' &&
             (text[1] == 'x' || text[1] == 'X')) { base = 16; start = 2; }

    char       *end  = nullptr;
    const char *begin = text.c_str() + start;
    const unsigned long value = std::strtoul(begin, &end, base);
    if (end == begin || *end != '\0') return false;

    out = static_cast<u32>(value);
    return true;
}

// ---------------------------------------------------------------------------
//  Stepping
// ---------------------------------------------------------------------------
// Returns false when the CPU can no longer make progress.
bool step_once(GameBoy &gb, bool trace)
{
    if (trace) {
        const Instruction ins = disassemble(gb.bus(), gb.cpu().regs().pc);
        std::printf("  $%04X   %-9s  %s\n",
                    ins.address, ins.hex().c_str(), ins.text.c_str());
    }
    gb.step();

    if (gb.cpu().illegal()) {
        std::printf("  stopped: illegal opcode $%02X\n", gb.cpu().illegal_opcode());
        return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
int run_debugger(GameBoy &gb)
{
    std::set<u16> breakpoints;

    std::printf("RetroEmu debugger. Type 'h' for the command list.\n\n");
    print_registers(gb);
    std::printf("\n");
    print_disassembly(gb, gb.cpu().regs().pc, 1);

    std::string line;
    for (;;) {
        std::printf("\n(retroemu) ");
        std::fflush(stdout);

        if (!std::getline(std::cin, line)) { std::printf("\n"); break; }

        std::istringstream        stream(line);
        std::vector<std::string>  words;
        std::string               word;
        while (stream >> word) words.push_back(word);
        if (words.empty()) continue;

        const std::string &cmd = words[0];
        u32 arg1 = 0, arg2 = 0;
        const bool has1 = words.size() > 1 && parse_number(words[1], arg1);
        const bool has2 = words.size() > 2 && parse_number(words[2], arg2);

        if (cmd == "q" || cmd == "quit") break;
        if (cmd == "h" || cmd == "help" || cmd == "?") { print_help(); continue; }

        if (cmd == "r" || cmd == "regs") {
            print_registers(gb);
            std::printf("\n");
            print_disassembly(gb, gb.cpu().regs().pc, 1);
            continue;
        }

        if (cmd == "d" || cmd == "dis") {
            const u16 addr  = has1 ? static_cast<u16>(arg1) : gb.cpu().regs().pc;
            const int count = has2 ? static_cast<int>(arg2) : 8;
            print_disassembly(gb, addr, count);
            continue;
        }

        if (cmd == "s" || cmd == "step") {
            const int count = has1 ? static_cast<int>(arg1) : 1;
            for (int i = 0; i < count; ++i) {
                if (!step_once(gb, /*trace=*/count <= 32)) break;
            }
            if (count > 32) std::printf("  stepped %d instructions\n", count);
            std::printf("\n");
            print_registers(gb);
            std::printf("\n");
            print_disassembly(gb, gb.cpu().regs().pc, 1);
            continue;
        }

        // Section V.2 of the subject: run one frame, and run one second.
        if (cmd == "f" || cmd == "frame") {
            const int count = has1 ? static_cast<int>(arg1) : 1;
            const u64 before = gb.bus().clock().t_sys();
            for (int i = 0; i < count; ++i) gb.run_frame();
            std::printf("  ran %d frame(s), %llu system cycles\n", count,
                        static_cast<unsigned long long>(gb.bus().clock().t_sys() - before));
            std::printf("\n");
            print_registers(gb);
            continue;
        }

        if (cmd == "t" || cmd == "time") {
            const double seconds = has1 ? static_cast<double>(arg1) : 1.0;
            const u64 before = gb.bus().clock().t_sys();
            gb.run_seconds(seconds);
            std::printf("  ran %.0f second(s), %llu system cycles\n", seconds,
                        static_cast<unsigned long long>(gb.bus().clock().t_sys() - before));
            std::printf("\n");
            print_registers(gb);
            continue;
        }

        if (cmd == "c" || cmd == "continue") {
            if (breakpoints.empty()) {
                std::printf("  no breakpoint set; 'c' would never return\n");
                continue;
            }
            // A bound is kept so a wrong breakpoint cannot hang the session.
            const u64 limit = gb.bus().clock().t_cpu() + 500000000ULL;
            bool hit = false;
            while (gb.bus().clock().t_cpu() < limit) {
                if (!step_once(gb, /*trace=*/false)) break;
                if (breakpoints.count(gb.cpu().regs().pc)) { hit = true; break; }
            }
            std::printf(hit ? "  breakpoint hit\n" : "  cycle limit reached\n");
            std::printf("\n");
            print_registers(gb);
            std::printf("\n");
            print_disassembly(gb, gb.cpu().regs().pc, 1);
            continue;
        }

        if (cmd == "b") {
            if (!has1) { std::printf("  usage: b <addr>\n"); continue; }
            breakpoints.insert(static_cast<u16>(arg1));
            std::printf("  breakpoint set at $%04X\n", static_cast<u16>(arg1));
            continue;
        }
        if (cmd == "bd") {
            if (!has1) { std::printf("  usage: bd <addr>\n"); continue; }
            breakpoints.erase(static_cast<u16>(arg1));
            std::printf("  breakpoint cleared at $%04X\n", static_cast<u16>(arg1));
            continue;
        }
        if (cmd == "bl") {
            if (breakpoints.empty()) { std::printf("  no breakpoints\n"); continue; }
            for (u16 bp : breakpoints) std::printf("  $%04X\n", bp);
            continue;
        }

        if (cmd == "m" || cmd == "mem") {
            if (!has1) { std::printf("  usage: m <addr> [count]\n"); continue; }
            print_memory(gb, static_cast<u16>(arg1), has2 ? static_cast<int>(arg2) : 64);
            continue;
        }

        if (cmd == "w") {
            if (!has1 || !has2) { std::printf("  usage: w <addr> <value>\n"); continue; }
            gb.bus().poke(static_cast<u16>(arg1), static_cast<u8>(arg2));
            std::printf("  $%04X = %02X\n", static_cast<u16>(arg1), static_cast<u8>(arg2));
            continue;
        }

        if (cmd == "i" || cmd == "info") { print_state(gb); continue; }

        if (cmd == "reset") {
            gb.reset();
            std::printf("  machine reset\n\n");
            print_registers(gb);
            continue;
        }

        std::printf("  unknown command '%s'; type 'h' for help\n", cmd.c_str());
    }

    return 0;
}

}  // namespace retroemu
