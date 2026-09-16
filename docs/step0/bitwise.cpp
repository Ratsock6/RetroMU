// ===========================================================================
//  RetroEmu — Step 0: hexadecimal / binary / bitwise warm-up
// ===========================================================================
//
//  This file is NOT part of the emulator. It is a training ground. It lives
//  in docs/ and is not built by CMake.
//
//  Build and run:
//      g++ -std=c++17 -Wall -Wextra -o /tmp/bitwise docs/step0/bitwise.cpp
//      /tmp/bitwise
//
//  Goal: make automatic the six gestures you will repeat thousands of times
//  in this project. Until section 7 (exercises) feels obvious, do not move on
//  to step 3 (the Bus).
// ===========================================================================

#include <cstdint>
#include <cstdio>
#include <string>

// --- Display helper: show a byte in binary, grouped by nibbles ------------
static std::string bin8(std::uint8_t v)
{
    std::string s;
    for (int bit = 7; bit >= 0; --bit) {
        s += ((v >> bit) & 1) ? '1' : '0';
        if (bit == 4) s += ' ';          // split the two nibbles: 0011 1100
    }
    return s;
}

static std::string bin16(std::uint16_t v)
{
    std::string s;
    for (int bit = 15; bit >= 0; --bit) {
        s += ((v >> bit) & 1) ? '1' : '0';
        if (bit % 4 == 0 && bit != 0) s += ' ';
    }
    return s;
}

static void heading(const char *t)
{
    std::printf("\n\033[1;36m=== %s ===\033[0m\n", t);
}

// ===========================================================================
//  1. HEXADECIMAL: why it is everywhere in this project
// ===========================================================================
static void section_hex()
{
    heading("1. HEXADECIMAL");
    std::puts("One hex digit is exactly 4 bits (one nibble). That is THE reason\n"
              "all hardware documentation is written in hex: converting between\n"
              "hex and binary is mechanical, digit by digit.\n");

    std::printf("  %-8s %-12s %s\n", "hex", "binary", "decimal");
    std::printf("  %-8s %-12s %s\n", "---", "------", "-------");
    const std::uint8_t table[] = {0x00, 0x01, 0x0F, 0x10, 0x3C, 0x80, 0xA5, 0xFF};
    for (std::uint8_t v : table)
        std::printf("  0x%02X     %-12s %u\n", v, bin8(v).c_str(), v);

    std::puts("\n  Worth memorising:  0x0F = 0000 1111  (low nibble full)");
    std::puts("                     0xF0 = 1111 0000  (high nibble full)");
    std::puts("                     0xFF = 1111 1111  (255, max value of a byte)");
    std::puts("                     0x80 = 1000 0000  (bit 7 only)");
}

// ===========================================================================
//  2. THE SIX FUNDAMENTAL OPERATIONS
// ===========================================================================
static void section_operations()
{
    heading("2. THE SIX FUNDAMENTAL OPERATIONS");

    const std::uint8_t a = 0x3C;   // 0011 1100
    const std::uint8_t b = 0x0F;   // 0000 1111

    std::printf("  a    = 0x%02X = %s\n", a, bin8(a).c_str());
    std::printf("  b    = 0x%02X = %s\n\n", b, bin8(b).c_str());

    // AND: keep a bit only if BOTH are set -> used to MASK
    std::printf("  a & b  = 0x%02X = %s   AND : keep only common bits (MASK)\n",
                a & b, bin8(a & b).c_str());

    // OR: keep the bit if AT LEAST ONE is set -> used to SET bits
    std::printf("  a | b  = 0x%02X = %s   OR  : turn bits on (SET)\n",
                a | b, bin8(a | b).c_str());

    // XOR: set if the two differ -> used to FLIP selected bits
    std::printf("  a ^ b  = 0x%02X = %s   XOR : flip the selected bits\n",
                a ^ b, bin8(a ^ b).c_str());

    // NOT: flip everything -> used to build a clear-mask
    std::printf("  ~a     = 0x%02X = %s   NOT : flip ALL bits\n",
                static_cast<std::uint8_t>(~a), bin8(static_cast<std::uint8_t>(~a)).c_str());

    // SHIFTS: multiply/divide by two, or move a bit field
    std::printf("  a << 1 = 0x%02X = %s   <<  : shift left (x2)\n",
                static_cast<std::uint8_t>(a << 1), bin8(static_cast<std::uint8_t>(a << 1)).c_str());
    std::printf("  a >> 1 = 0x%02X = %s   >>  : shift right (/2)\n",
                static_cast<std::uint8_t>(a >> 1), bin8(static_cast<std::uint8_t>(a >> 1)).c_str());

    std::puts("\n  \033[1;33mC++ TRAP\033[0m: a << 1 first promotes a to int (32 bits).");
    std::puts("  Without the uint8_t cast, 0x80 << 1 gives 0x100, not 0x00!");
}

// ===========================================================================
//  3. THE FOUR IDIOMS YOU WILL WRITE MOST OFTEN
// ===========================================================================
static void section_idioms()
{
    heading("3. THE FOUR IDIOMS OF THIS PROJECT");

    std::uint8_t reg = 0x00;

    // (a) TEST a bit -> "is the screen on?" (bit 7 of LCDC)
    std::printf("  TEST   : (v >> n) & 1\n");
    reg = 0x80;
    std::printf("           reg=0x%02X, bit 7 = %u  -> LCD is on\n", reg, (reg >> 7) & 1);

    // (b) SET a bit -> "raise the VBlank interrupt flag" (bit 0 of IF)
    std::printf("\n  SET    : v |= (1 << n)\n");
    reg = 0x00;
    std::printf("           before reg=0x%02X = %s\n", reg, bin8(reg).c_str());
    reg |= (1 << 0);
    std::printf("           after  reg=0x%02X = %s  -> VBlank IRQ raised\n", reg, bin8(reg).c_str());

    // (c) CLEAR a bit -> "acknowledge the interrupt"
    std::printf("\n  CLEAR  : v &= ~(1 << n)\n");
    reg = 0xFF;
    std::printf("           before reg=0x%02X = %s\n", reg, bin8(reg).c_str());
    reg &= static_cast<std::uint8_t>(~(1 << 3));
    std::printf("           after  reg=0x%02X = %s  -> bit 3 cleared\n", reg, bin8(reg).c_str());

    // (d) EXTRACT a bit field -> decode an opcode
    std::printf("\n  EXTRACT: (v >> start) & mask\n");
    const std::uint8_t opcode = 0x42;   // 0100 0010 = LD B, D
    const std::uint8_t dst = (opcode >> 3) & 0x07;   // bits 3-5
    const std::uint8_t src = (opcode >> 0) & 0x07;   // bits 0-2
    std::printf("           opcode 0x%02X = %s\n", opcode, bin8(opcode).c_str());
    std::printf("           destination = (op >> 3) & 0x07 = %u   (0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A)\n", dst);
    std::printf("           source      = (op >> 0) & 0x07 = %u\n", src);
    std::printf("           => \033[1;32mLD B, D\033[0m  -- decoded WITHOUT any switch/case!\n");
}

// ===========================================================================
//  4. REGISTER PAIRS: composing and splitting 16-bit values
// ===========================================================================
static void section_pairs()
{
    heading("4. REGISTER PAIRS (H and L <-> HL)");

    std::uint8_t h = 0x12;
    std::uint8_t l = 0x34;

    // Compose: the HIGH byte moves up 8 bits, the LOW byte stays in place.
    std::uint16_t hl = static_cast<std::uint16_t>((h << 8) | l);
    std::printf("  h = 0x%02X = %s\n", h, bin8(h).c_str());
    std::printf("  l = 0x%02X = %s\n", l, bin8(l).c_str());
    std::printf("  hl = (h << 8) | l = 0x%04X = %s\n\n", hl, bin16(hl).c_str());

    // Split: the other way round.
    hl = 0xABCD;
    h = static_cast<std::uint8_t>(hl >> 8);      // high byte
    l = static_cast<std::uint8_t>(hl & 0x00FF);  // low byte
    std::printf("  hl = 0x%04X  ->  h = hl >> 8 = 0x%02X   l = hl & 0xFF = 0x%02X\n", 0xABCD, h, l);

    std::puts("\n  \033[1;33mNOTE\033[0m: this CPU is LITTLE-ENDIAN. In memory the address");
    std::puts("  0xABCD is stored as CD then AB (low byte first).");
}

// ===========================================================================
//  5. WRAP-AROUND: the number one beginner bug
// ===========================================================================
static void section_wraparound()
{
    heading("5. WRAP-AROUND (silent overflow)");

    std::uint8_t x = 255;
    std::printf("  uint8_t x = 255;  x++;   ->  x = %u    (not 256!)\n", ++x);

    std::uint8_t y = 0;
    std::printf("  uint8_t y = 0;    y--;   ->  y = %u  (not -1!)\n", --y);

    std::uint16_t pc = 0xFFFF;
    std::printf("  uint16_t pc = 0xFFFF; pc++; ->  pc = 0x%04X\n", ++pc);

    std::puts("\n  This is how the REAL hardware behaves: registers are a fixed");
    std::puts("  number of wires, they physically cannot 'overflow'.");
    std::puts("  \033[1;31mDANGER\033[0m: writing 'int a = 255; a++;' gives 256 and your");
    std::puts("  emulator becomes wrong. ALWAYS use uint8_t / uint16_t.");
}

// ===========================================================================
//  6. THE REAL EXERCISE: ADD A, B and its four flags
// ===========================================================================
static void add_a_b(std::uint8_t a, std::uint8_t b)
{
    // Compute on 16 bits so the outgoing carry is visible.
    const std::uint16_t wide = static_cast<std::uint16_t>(a) + static_cast<std::uint16_t>(b);
    const std::uint8_t  res  = static_cast<std::uint8_t>(wide);

    const bool Z = (res == 0);                              // result is zero
    const bool N = false;                                   // not a subtraction
    const bool H = ((a & 0x0F) + (b & 0x0F)) > 0x0F;        // carry from bit 3 to bit 4
    const bool C = (wide > 0xFF);                            // carry out of bit 7

    std::printf("  ADD A,B  A=0x%02X B=0x%02X  ->  A=0x%02X   Z=%d N=%d H=%d C=%d\n",
                a, b, res, Z, N, H, C);
}

static void section_flags()
{
    heading("6. ADD A, B: THE FOUR FLAGS (the heart of the CPU)");

    std::puts("  Z = result is zero | N = the operation was a subtraction");
    std::puts("  H = carry from the low nibble into the high one (half-carry)");
    std::puts("  C = carry out of the byte\n");

    add_a_b(0x0F, 0x01);   // H only: 0x0F + 0x01 overflows the low nibble
    add_a_b(0xFF, 0x01);   // everything: result 0x00 -> Z, H and C
    add_a_b(0x00, 0x00);   // Z only
    add_a_b(0x08, 0x08);   // H only (8+8 = 16, overflows the nibble)
    add_a_b(0xF0, 0x10);   // C only, no H
    add_a_b(0x3A, 0x05);   // no flag at all

    std::puts("\n  \033[1;33mTHE HALF-CARRY IS THE NUMBER ONE BUG OF THIS PROJECT.\033[0m");
    std::puts("  The formula to carve in stone:  H = ((a & 0x0F) + (b & 0x0F)) > 0x0F");
    std::puts("  In words: add ONLY the low nibbles and check whether that");
    std::puts("  overflows. The H flag is what the DAA instruction relies on.");
}

// ===========================================================================
//  7. EXERCISES: answer BEFORE reading the result
// ===========================================================================
static void section_exercises()
{
    heading("7. EXERCISES (cover the right-hand column)");

    struct Exercise { const char *question; unsigned answer; const char *why; };

    const Exercise exercises[] = {
        {"0x0F & 0xF0",                       0x0F & 0xF0, "no bit in common"},
        {"0x3C | 0xC3",                       0x3C | 0xC3, "0011 1100 | 1100 0011"},
        {"0xFF ^ 0x0F",                       0xFF ^ 0x0F, "XOR with 0x0F flips the low nibble"},
        {"(0xB7 >> 4) & 0x0F",               (0xB7 >> 4) & 0x0F, "extract the HIGH nibble of 0xB7"},
        {"0xB7 & 0x0F",                       0xB7 & 0x0F, "extract the LOW nibble of 0xB7"},
        {"(0x47 >> 3) & 0x07",               (0x47 >> 3) & 0x07, "destination field of opcode 0x47"},
        {"1 << 6",                            1u << 6,     "set bit 6"},
        {"(uint8_t)~(1 << 2)",     (unsigned)(std::uint8_t)~(1 << 2), "mask used to CLEAR bit 2"},
        {"(0x12 << 8) | 0x34",               (0x12 << 8) | 0x34, "compose HL from H=0x12 L=0x34"},
        {"(uint8_t)(0x80 << 1)",   (unsigned)(std::uint8_t)(0x80 << 1), "bit 7 shifts out and is lost"},
    };

    std::printf("  %-28s | %s\n", "expression", "result");
    std::printf("  %-28s-+-%s\n", "----------------------------", "------");
    for (const Exercise &e : exercises)
        std::printf("  %-28s | 0x%02X  (%s)\n", e.question, e.answer, e.why);

    std::puts("\n  \033[1;32mCHECKPOINT\033[0m: if you can produce all ten results from memory,");
    std::puts("  you are ready for step 3 (the Bus).");
}

int main()
{
    std::puts("\n\033[1;35m##########################################################\033[0m");
    std::puts("\033[1;35m#   RetroEmu - Step 0: hexadecimal and bitwise warm-up   #\033[0m");
    std::puts("\033[1;35m##########################################################\033[0m");

    section_hex();
    section_operations();
    section_idioms();
    section_pairs();
    section_wraparound();
    section_flags();
    section_exercises();

    std::puts("");
    return 0;
}
