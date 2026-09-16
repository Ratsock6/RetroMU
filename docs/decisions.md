# Technical decision log — RetroEmu

This file exists for the defence. Whenever an ambiguity in the subject has to
be resolved, the decision is recorded here together with the alternatives that
were considered, so that it can be justified in front of a corrector.

Reference: subject **RetroEmu**, version 9.1, 12 pages.

---

## Ambiguities found in the subject

The subject does not specify the following points. Each one had to be decided.

| # | Unspecified point | Where |
|---|---|---|
| A1 | "Running thread": is a real thread required, or only a non-degraded GUI? | V.3, p.7 |
| A2 | Are libraries outside the rendering layer free to use? (the Dear ImGui case) | Ch. IV, p.6 |
| A3 | Does the post-boot register state belong to the mandatory part or to the bonus? | V.1 p.7 / Ch. VI p.9 |
| A4 | What timing accuracy is required? No figure is given. | V.3, p.7 |
| A5 | Are command-line arguments allowed in addition to the GUI? | Ch. IV, p.6 |
| A6 | Which errors must be handled? No requirement is written. | — |
| A7 | Where should the debugger live (terminal, window, overlay)? | V.1, p.7 |

---

## D1 — SDL2: hybrid build strategy

**Context.** The subject (Ch. IV, p.6) requires the build to "bundle or
document its runtime dependencies so the corrector can run it without manual
setup".

**Alternatives.**
1. `find_package(SDL2 REQUIRED)` + a README saying "install libsdl2-dev" —
   fails if the corrector has no SDL2, which is exactly manual setup.
2. Always use `FetchContent` — works everywhere, but rebuilds SDL2 even when
   the system already provides it.
3. **Hybrid**: `find_package` first, automatic fallback to `FetchContent` with
   a static build.

**Decision: option 3.** See `cmake/SDL2Setup.cmake`.
On the fallback path SDL2 is linked **statically**: the resulting executable
depends on no `libSDL2.so`, which satisfies the subject's verb "bundle"
literally.

**Verified on 2026-09-16** on a container with no SDL2 installed: fresh clone,
single command, working executable, zero external SDL dependency.

---

## D2 — Test ROMs are versioned

**Context.** The subject (p.6, p.11) forbids **commercial** ROMs in the
repository. It asks (p.11) that `attachments/test-roms.zip` be extracted
"next to your executable".

**Decision.** The bundle is committed under `roms/`, `LICENSE` files included
(the 9 ROMs are MIT-licensed: Matt Currie for acid2, Joonas Javanainen for
mooneye). The corrector therefore has nothing to download.
Development ROMs (Blargg, personally owned dumps) live in `roms-dev/`, which
is gitignored.

---

## D3 — Memory ownership: the Bus owns everything

**Alternatives.** Global memory; cross pointers between components; a single
owner.

**Decision.** `Bus` owns the cartridge, the PPU, the timer, the joypad, the
DMA engine, WRAM and HRAM. `Cpu` owns nothing and receives `Bus&` as a
parameter: `u32 Cpu::step(Bus& bus);`
No circular references, and every component stays testable in isolation.

---

## D4 — The debugger lives in the terminal

**Context (A2, A7).** Dear ImGui would look nicer, but the subject forbids
"any higher-level framework for the rendering layer" (p.6) without saying
whether ImGui counts as one. Grey area.

**Decision.** Terminal debugger. No risk at the defence, scriptable, and
available immediately. An overlay drawn directly with SDL2 may be added in
step 15 if time allows.

---

## D5 — PPU renders per scanline

**Alternatives.** Per frame (too coarse, fails dmg-acid2); **per scanline**;
per pixel with a hardware-accurate FIFO (exact, but very expensive).

**Decision.** Scanline rendering, composed at the end of mode 3.
Sufficient for both acid2 ROMs in the bundle. The FIFO is only needed for
mid-scanline effects that no ROM in the bundle exercises.

---

## D6 — Pacing by clock, not by VSync

**Decision.** No `SDL_RENDERER_PRESENTVSYNC`. VSync would lock emulation to
the corrector's monitor refresh rate (60 Hz) while the hardware runs at
59.727 Hz. Pacing will be driven by a high-resolution clock (step 11).

---

## D7 — Single-threaded loop

**Context (A1).** The title of section V.3 reads "Running thread", but the
body only requires "normal speed without adversely affecting the operations of
the GUI" (p.7).

**Decision.** Single-threaded loop: emulate one frame, pump events, present,
wait. The written requirement is met. SDL2 requires events to be pumped on the
thread that owns the window anyway, which makes multithreading more of a risk
than a benefit here.

**To defend at the review** if a corrector reads the section title literally.

---

## D8 — M-cycle timing through "tick-on-access"

**The most structural decision of the project.**

**Context (A4).** The subject gives no timing accuracy figure. The test bundle
sets the bar instead: it contains
`mooneye/acceptance/div_timing.gb`, `intr_timing.gb` and `oam_dma/basic.gb`,
which check on exactly which cycle, **inside an instruction**, the DIV
register increments and the IF flag is sampled.

**Alternatives.**
1. Catch-up at the end of each instruction (`n = cpu.step(); ppu.tick(n);`) —
   simple, but cannot pass those three ROMs.
2. **Tick-on-access**: every memory access advances the clock by 4 cycles at
   the moment it happens.
3. T-cycle FIFO — exact, but out of proportion for this bundle.

**Decision: option 2.**
```cpp
u8 Bus::read(u16 addr) { tick(4); return dispatch(addr); }
```
Corollary: time is counted in two distinct units from the start, `t_cpu` and
`t_sys`, because in CGB double-speed mode (section V.6, p.8) the CPU runs
twice as fast but the PPU does **not**.

Putting that separation in place now costs three lines; retrofitting it later
would mean rewriting the CPU, the PPU, the timer and the DMA engine.

---

## D9 — Command-line arguments in addition to the GUI

**Context (A5).** The subject requires a GUI with "load, play, pause" (p.6)
and says nothing about a command-line interface.

**Decision.** The GUI remains the required loading mechanism. An optional
`./retroemu [rom]` argument is accepted on top of it: it takes nothing away
from the requirement and greatly speeds up the development loop.

A windowless `--selftest` mode is also provided so the graphics pipeline can
be validated over SSH or inside a container.

---

## D10 — Language of the code and of the output

**Context.** The subject is explicit: "**No coding style is enforced.** You
may follow any convention you like as long as your code remains readable to
your peer evaluators." (p.6). No language constraint exists.

**Decision.** Everything in English: identifiers, comments, program output,
README and documentation. Identifiers already follow hardware terminology
(`LCDC`, `SCX`, `OAM`), so a single language throughout keeps the codebase
consistent and readable by any evaluator.

---

## D11 — The Nintendo logo is not verified

**Context.** Real hardware compares the 48 bytes at 0x0104-0x0133 against a
copy held in the boot ROM and refuses to run the cartridge when they differ.

**Decision.** That check is deliberately not implemented. Performing it would
mean embedding the logo bitmap in this repository, and the subject (Ch. VI,
p.9) states that this artwork is protected and must not be shipped. Since the
mandatory part skips the boot ROM entirely (ambiguity A3), nothing depends on
it: every ROM is accepted regardless of the logo bytes.

Should the "Boot sequence" bonus be attempted later, the logo would come from
the open-source boot ROM chosen at that point, never from this repository.

---

## D12 — Error handling policy

**Context (A6).** The subject states no error-handling requirement at all.

**Decision.** Two tiers, so that nothing ever crashes in front of a corrector:

- **Fatal** (load refused, exit code 1): the file cannot be opened, is empty,
  or is shorter than the 336-byte header. There is nothing to emulate.
- **Warning** (load succeeds, message printed): bad header checksum, ROM size
  that disagrees with the file size, unknown cartridge type or RAM size code.
  Real hardware would halt on a bad header checksum, but since the mandatory
  part skips the boot ROM there is no reason to refuse the ROM.

Rationale: a corrector handing over an unusual homebrew ROM should see a clear
diagnostic, not a crash and not a silent wrong result.
