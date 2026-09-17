#include "retroemu/debug/disassembler.hpp"
#include <cstdarg>

#include <cstdio>

namespace retroemu {
namespace {

// The same operand tables the CPU decoder indexes into.
const char *kR8[8]   = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
const char *kRp[4]   = {"BC", "DE", "HL", "SP"};
const char *kRp2[4]  = {"BC", "DE", "HL", "AF"};
const char *kCc[4]   = {"NZ", "Z", "NC", "C"};
const char *kAlu[8]  = {"ADD A,", "ADC A,", "SUB", "SBC A,", "AND", "XOR", "OR", "CP"};
const char *kRot[8]  = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SWAP", "SRL"};

std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

std::string format(const char *fmt, ...)
{
    char    buffer[96];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

}  // namespace

std::string Instruction::hex() const
{
    std::string s;
    for (u8 i = 0; i < length; ++i) {
        if (i) s += ' ';
        char b[4];
        std::snprintf(b, sizeof(b), "%02X", bytes[i]);
        s += b;
    }
    return s;
}

Instruction disassemble(const Bus &bus, u16 address)
{
    Instruction ins;
    ins.address = address;

    // peek, never read: inspecting must not advance the clock (D15).
    const u8 opcode = bus.peek(address);
    const u8 imm8   = bus.peek(static_cast<u16>(address + 1));
    const u8 imm8h  = bus.peek(static_cast<u16>(address + 2));
    const u16 imm16 = static_cast<u16>((imm8h << 8) | imm8);
    const i8  disp  = static_cast<i8>(imm8);

    // Where a relative jump would land. Relative to the byte AFTER the
    // two-byte instruction, which is why the +2 is there.
    const u16 jr_target = static_cast<u16>(address + 2 + disp);

    ins.bytes[0] = opcode;
    ins.bytes[1] = imm8;
    ins.bytes[2] = imm8h;

    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 0x07;
    const int z = opcode & 0x07;
    const int p = y >> 1;
    const int q = y & 0x01;

    auto one   = [&](const std::string &t) { ins.length = 1; ins.text = t; };
    auto two   = [&](const std::string &t) { ins.length = 2; ins.text = t; };
    auto three = [&](const std::string &t) { ins.length = 3; ins.text = t; };

    switch (x) {

    case 0:
        switch (z) {
            case 0:
                if (y == 0) { one("NOP"); return ins; }
                if (y == 1) { three(format("LD ($%04X), SP", imm16)); return ins; }
                if (y == 2) { two("STOP"); return ins; }
                if (y == 3) { two(format("JR $%04X", jr_target)); return ins; }
                two(format("JR %s, $%04X", kCc[y - 4], jr_target));
                return ins;

            case 1:
                if (q == 0) { three(format("LD %s, $%04X", kRp[p], imm16)); return ins; }
                one(format("ADD HL, %s", kRp[p]));
                return ins;

            case 2: {
                const char *target = (p == 0) ? "(BC)" : (p == 1) ? "(DE)"
                                   : (p == 2) ? "(HL+)" : "(HL-)";
                one(q == 0 ? format("LD %s, A", target) : format("LD A, %s", target));
                return ins;
            }

            case 3: one(format("%s %s", q == 0 ? "INC" : "DEC", kRp[p])); return ins;
            case 4: one(format("INC %s", kR8[y])); return ins;
            case 5: one(format("DEC %s", kR8[y])); return ins;
            case 6: two(format("LD %s, $%02X", kR8[y], imm8)); return ins;

            default: {
                const char *names[8] = {"RLCA", "RRCA", "RLA", "RRA",
                                        "DAA", "CPL", "SCF", "CCF"};
                one(names[y]);
                return ins;
            }
        }

    case 1:
        if (y == 6 && z == 6) { one("HALT"); return ins; }
        one(format("LD %s, %s", kR8[y], kR8[z]));
        return ins;

    case 2:
        one(format("%s %s", kAlu[y], kR8[z]));
        return ins;

    default:
        switch (z) {
            case 0:
                if (y < 4)  { one(format("RET %s", kCc[y])); return ins; }
                if (y == 4) { two(format("LDH ($%02X), A", imm8)); return ins; }
                if (y == 5) { two(format("ADD SP, %d", disp)); return ins; }
                if (y == 6) { two(format("LDH A, ($%02X)", imm8)); return ins; }
                two(format("LD HL, SP%+d", disp));
                return ins;

            case 1:
                if (q == 0) { one(format("POP %s", kRp2[p])); return ins; }
                switch (p) {
                    case 0:  one("RET");        return ins;
                    case 1:  one("RETI");       return ins;
                    case 2:  one("JP HL");      return ins;
                    default: one("LD SP, HL");  return ins;
                }

            case 2:
                if (y < 4) { three(format("JP %s, $%04X", kCc[y], imm16)); return ins; }
                switch (y) {
                    case 4:  one("LDH ($FF00+C), A"); return ins;
                    case 5:  three(format("LD ($%04X), A", imm16)); return ins;
                    case 6:  one("LDH A, ($FF00+C)"); return ins;
                    default: three(format("LD A, ($%04X)", imm16)); return ins;
                }

            case 3:
                if (y == 0) { three(format("JP $%04X", imm16)); return ins; }
                if (y == 1) {                      // the 0xCB page
                    const u8  cb   = imm8;
                    const int cb_x = cb >> 6;
                    const int cb_y = (cb >> 3) & 0x07;
                    const int cb_z = cb & 0x07;
                    if (cb_x == 0)      two(format("%s %s", kRot[cb_y], kR8[cb_z]));
                    else if (cb_x == 1) two(format("BIT %d, %s", cb_y, kR8[cb_z]));
                    else if (cb_x == 2) two(format("RES %d, %s", cb_y, kR8[cb_z]));
                    else                two(format("SET %d, %s", cb_y, kR8[cb_z]));
                    return ins;
                }
                if (y == 6) { one("DI"); return ins; }
                if (y == 7) { one("EI"); return ins; }
                one(format("<illegal $%02X>", opcode));
                return ins;

            case 4:
                if (y < 4) { three(format("CALL %s, $%04X", kCc[y], imm16)); return ins; }
                one(format("<illegal $%02X>", opcode));
                return ins;

            case 5:
                if (q == 0) { one(format("PUSH %s", kRp2[p])); return ins; }
                if (p == 0) { three(format("CALL $%04X", imm16)); return ins; }
                one(format("<illegal $%02X>", opcode));
                return ins;

            case 6: two(format("%s $%02X", kAlu[y], imm8)); return ins;

            default: one(format("RST $%02X", y * 8)); return ins;
        }
    }
}

}  // namespace retroemu
