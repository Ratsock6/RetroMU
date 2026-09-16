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
| 3 | Bus / MMU with tick-on-access | todo |
| 4 | CPU: registers, flags, instruction set | todo |
| 5 | Disassembler and debugger *(subject V.1)* | todo |
| 6 | Trace log and differential validation | todo |
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
./tests/run_cartridge_tests.sh
```

Checks the parsed summary of the nine bundled ROMs against a golden file and
exercises the malformed-input paths (missing, empty, truncated and corrupted
files). Pass `--update` to regenerate the golden file after an intentional
format change.

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
├── roms/                   MIT test bundle (versioned)
├── roms-dev/               development ROMs (gitignored)
├── tests/                  regression scripts and golden files
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
