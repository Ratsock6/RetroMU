# RetroEmu

An emulator for an 8-bit handheld console — the **DMG** model (1989) and its
colour successor, the **CGB** (1998).

42 School project — subject *RetroEmu*, version 9.1.

---

## Building

**A single command**, as required by the subject (Chapter IV, p.6):

```bash
cmake -B build && cmake --build build -j
```

The executable is then `./build/retroemu`.

### Dependencies

The only dependency is **SDL2**, imposed by the subject for the graphics,
input and audio layers (p.6).

**No manual installation is required.** The build detects SDL2:

- if it is present on the system, it is used as is (fast build);
- otherwise CMake downloads SDL2 and builds it **statically**. The resulting
  executable embeds SDL2 and depends on no `libSDL2.so`.

In the second case the first build takes a few minutes; later builds are
cached.

Actual prerequisites: a C++17 compiler (GCC 7+ or Clang 6+), CMake 3.16+, and
`git` if SDL2 has to be downloaded.

### Build options

| Option | Default | Effect |
|---|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | `Release` | Unoptimised debug build |
| `-DRETROEMU_SANITIZE=ON` | `OFF` | Enable AddressSanitizer and UBSan |
| `-DRETROEMU_FORCE_FETCH_SDL2=ON` | `OFF` | Ignore system SDL2 and rebuild it |
| `-DRETROEMU_WARNINGS=OFF` | `ON` | Disable strict warnings |

---

## Usage

```bash
./build/retroemu --info roms/acid2/dmg-acid2.gb    # decode one cartridge header
./build/retroemu --list roms/**/*.gb               # one summary line per ROM
./build/retroemu --memtest roms/acid2/dmg-acid2.gb # walk the memory map, check the clock
./build/retroemu --cpucheck                        # built-in CPU self-test
./build/retroemu --run roms/acid2/dmg-acid2.gb     # run headless, print the link-port output
./build/retroemu --debug roms/acid2/dmg-acid2.gb   # interactive debugger
./build/retroemu --discheck roms/*/*.gb            # disassembler vs CPU
./build/retroemu --trace roms/acid2/dmg-acid2.gb --trace-limit 20
./build/retroemu                      # open the window
./build/retroemu --scale 6            # window magnified x6
./build/retroemu --selftest           # check the graphics pipeline, no window
./build/retroemu --selftest out.ppm   # ... and save the produced image
./build/retroemu --help
```

`--selftest` opens no window, so the build can be validated over SSH, inside a
container, or anywhere without a display server.

---

## Progress

The project is developed step by step, each step validated against a ROM from
the test bundle before moving on.

| # | Step | State |
|---|---|---|
| 0 | Hexadecimal / bitwise warm-up | done |
| 1 | CMake skeleton + SDL2 + 160x144 window | done |
| 2 | Cartridge: ROM loading and header parsing | done |
| 3 | Bus / MMU with tick-on-access | done |
| 4 | CPU: registers, flags, instruction set | done |
| 5 | Disassembler and debugger *(subject V.1, V.2)* | done |
| 6 | Trace log and differential validation | done |
| 7 | Interrupts and timer | todo |
| 8 | PPU: state machine | todo |
| 9 | PPU: background, window, sprites, palettes *(subject V.2)* | todo |
| 10 | OAM DMA | todo |
| 11 | Real-time loop and inputs *(subject V.3, V.4)* | todo |
| 12 | GUI: load / play / pause *(subject Chapter IV)* | todo |
| 13 | MBC1, MBC2, MBC5 and battery saves *(subject V.5)* | todo |
| 14 | CGB: palettes, VRAM bank, HDMA, double speed *(subject V.6)* | todo |
| 15 | Robustness and finalisation | todo |

---

## Tests

```bash
./tests/run_cartridge_tests.sh    # step 2: header parsing        (18 checks)
./tests/run_bus_tests.sh          # step 3: dispatch and clock    (30 checks)
./tests/run_cpu_tests.sh          # step 4: instruction set       (102 checks + blargg)
./tests/run_debug_tests.sh        # step 5: disassembler, debugger (22 checks)
./tests/run_trace_tests.sh        # step 6: tracer and fingerprints (11 checks)
```

`run_cartridge_tests.sh` checks the parsed summary of the nine bundled ROMs
against a golden file and exercises the malformed-input paths (missing, empty,
truncated and corrupted files). Pass `--update` to regenerate the golden file
after an intentional format change.

`run_bus_tests.sh` checks that every address reaches its owner, that ROM
rejects writes while RAM accepts them, that echo RAM mirrors correctly, and
that the clock charges exactly 4 T-cycles per access while the two clock
domains diverge in double-speed mode.

`run_cpu_tests.sh` runs the self-test built into the executable, then, if
`roms-dev/` has been populated, blargg's `cpu_instrs` suite. Those ROMs are not
committed — the subject only evaluates the MIT bundle (p.11) — so fetch them
separately:

```bash
./tools/fetch_dev_roms.sh
```

They report their verdict through the link port rather than the screen, which
is what makes it possible to validate the whole instruction set before any
rendering exists.

### Differential tracing

A broken emulator says nothing: no exception, no message, just a white screen,
and the mistake usually happened hundreds of thousands of instructions before
the symptom. The technique that works is to log the complete CPU state on
every instruction, produce the same log from an emulator known to be correct,
and compare. The first differing line names the exact instruction that went
wrong.

```bash
./build/retroemu --trace <rom> --trace-file mine.log --trace-limit 100000 --ly-stub
./build/retroemu --tracediff mine.log reference.log
```

The line format is the one the community's reference logs use:

```
A:01 F:B0 B:00 C:13 D:00 E:D8 H:01 L:4D SP:FFFE PC:0100 PCMEM:00,C3,50,01
```

`--ly-stub` makes LY (0xFF44) read back as 0x90, which those reference logs
require because they were recorded without a PPU.

`--tracediff` reports the first divergence with five lines of context and
names the register or flag that differs:

```
  first divergence at instruction 12
        11  A:01 F:30 ... PC:4800 PCMEM:F0,44,FE,90
        12  A:00 F:30 ... PC:4802 PCMEM:FE,90,20,FA   <- mine.log
        12  A:90 F:30 ... PC:4802 PCMEM:FE,90,20,FA   <- reference.log
  A      differs: 00 versus 90
```

`--tracehash` produces a fingerprint of a ROM's first instructions.
`tests/run_trace_tests.sh` compares those against a golden file, so any
unintended change in CPU behaviour during a later step is caught immediately.
When a step legitimately changes them, regenerate with `--update` and read the
diff to confirm the change was the intended one.

### The debugger

`--debug <rom>` opens a terminal debugger providing what section V.1 of the
subject requires — registers, the next instruction to execute, and single
stepping — plus the frame and one-second stepping of section V.2.

```
r                 registers, flags and interrupt state
d [addr] [n]      disassemble n instructions (default: from PC, 8)
s [n]             step n instructions
f [n]             run n frames
t [n]             run n seconds of emulation
c                 run until a breakpoint is hit
b / bd / bl       set, delete and list breakpoints
m <addr> [n]      dump memory
w <addr> <value>  write a byte without advancing the clock
i                 clocks, frames, link-port output
reset             reset the machine
q                 quit
```

It reads from stdin, so it can also be scripted:

```bash
printf 'b $0150\nc\nr\nq\n' | ./build/retroemu --debug roms/acid2/dmg-acid2.gb
```

All scripts honour `RETROEMU_BIN` if you want to point them at another
binary, for instance a sanitizer build:

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DRETROEMU_SANITIZE=ON
cmake --build build-asan -j
RETROEMU_BIN=$PWD/build-asan/retroemu ./tests/run_bus_tests.sh
```

---

## Repository layout

```
RetroEmu/
├── CMakeLists.txt          main build file
├── cmake/SDL2Setup.cmake   SDL2 detection with automatic fallback
├── include/retroemu/       headers
│   ├── core/               CPU, bus, PPU, timer, cartridge...
│   ├── debug/              disassembler, tracer, debugger
│   └── front/              SDL2, user interface
├── src/
│   ├── main.cpp            entry point and CLI
│   └── core/               emulator core (no SDL here)
│       ├── cartridge.cpp   ROM loading and header parsing
│       ├── bus.cpp         address dispatch and the master clock
│       ├── ppu.cpp         owns VRAM and OAM (rendering from step 9)
│       ├── cpu.cpp         the instruction set
│       └── gameboy.cpp     the assembled machine
│   └── debug/              debugger-side tooling
│       ├── cpu_selftest.cpp  self-contained instruction checks
│       ├── disassembler.cpp  bytes to text, no side effects
│       ├── debugger.cpp      terminal debugger (subject V.1, V.2)
│       └── tracer.cpp        execution trace and differential diff
├── roms/                   MIT test bundle (versioned)
├── roms-dev/               development ROMs (gitignored)
├── tests/                  regression scripts and golden files
├── tools/                  helper scripts (fetching dev ROMs)
└── docs/
    ├── decisions.md        technical decision log
    └── step0/bitwise.cpp   hexadecimal / bitwise training ground
```

---

## ROMs

`roms/` holds the test bundle shipped with the subject: 9 MIT-licensed ROMs
(acid2 by Matt Currie, mooneye by Joonas Javanainen). See `roms/README.md` for
what each of them validates.

**No commercial ROM is present in this repository**, per the subject (p.6 and
p.11).

---

## Hardware documentation

The emulated hardware is documented by the homebrew community, which the
subject explicitly points to (p.4): **Pan Docs** and the **GBDev wiki**.

The project's technical choices, along with the points the subject leaves
ambiguous, are recorded in [`docs/decisions.md`](docs/decisions.md).
