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

---

## D13 — The PPU owns VRAM and OAM from the start

**Context.** Step 3 needs somewhere to route 0x8000-0x9FFF and 0xFE00-0xFE9F,
but the PPU itself is not written until step 8.

**Alternatives.** Park the arrays inside the Bus and move them into the Ppu
class later; or create a minimal Ppu now that owns them.

**Decision.** Create the Ppu class immediately, owning VRAM and OAM and
accepting cycles, with no behaviour yet. Steps 8 and 9 then only add logic,
they never move data. The array is already sized for the CGB's two VRAM banks
(subject V.6, p.8), so enabling VBK later is a one-line change rather than a
reallocation.

---

## D14 — The clock owns the CPU-to-system conversion

**Context.** In CGB double-speed mode the CPU runs twice as fast while the PPU
does not, so any code that feeds CPU cycles to the PPU is wrong.

**Decision.** `Clock::advance(t)` takes CPU-domain cycles and **returns** how
many system-domain cycles elapsed. `Bus::tick` passes that return value to the
PPU rather than dividing by two itself. One place performs the conversion, so
the two domains cannot drift apart, and the odd remainder is carried rather
than truncated.

---

## D15 — `peek` is separate from `read`

**Context.** Section V.1 of the subject (p.7) requires a debugger that
displays the next instruction to execute. Disassembling means reading memory.

**Decision.** `Bus::read` advances the clock and may have side effects;
`Bus::peek` is `const`, advances nothing and has none. The debugger and the
disassembler use `peek` exclusively, so inspecting the machine can never
perturb the emulation it is inspecting.

---

## D16 — The CPU does not count its own cycles

**Context.** Every instruction has a documented duration, and the obvious
implementation is a table mapping opcode to cycle count.

**Decision.** No such table exists. Because `Bus::read` and `Bus::write`
already charge 4 T-cycles at the moment of each access (D8), the duration of
an instruction falls out of what it actually does. Only *internal* cycles,
those matching no memory access, are added explicitly with `bus.tick(4)`:
the extra cycle of `INC BC`, of a taken branch, of `PUSH`, and so on.

Consequence: a mistake in an instruction's shape shows up as a timing error
rather than staying invisible. The self-test checks eighteen instruction
durations against the documentation for exactly that reason.

---

## D17 — Opcodes are decoded by bit field, not by a 500-case switch

**Context.** There are 256 one-byte opcodes plus 256 behind the 0xCB prefix.

**Decision.** The opcode map is regular, so the fields are extracted:
`x = op >> 6`, `y = (op >> 3) & 7`, `z = op & 7`. The whole 0x40-0x7F range,
for instance, is `LD r[y], r[z]`, which collapses 64 cases into one line.
The result is shorter, and a typo in one case cannot silently affect only one
rare opcode.

---

## D18 — Interrupt dispatch belongs to step 4, the timer to step 7

**Context.** The plan lists "interrupts and timer" together as step 7.

**Decision.** The *mechanism* (IME, the one-instruction delay of `EI`, the
five-machine-cycle dispatch, `RETI`, waking from `HALT`, and the halt bug) is
part of the CPU and was implemented here. Step 7 adds the *sources*: DIV and
TIMA, plus the precise timing the mooneye acceptance ROMs check.

This is why blargg's `02-interrupts` currently reports "Timer doesn't work":
the dispatch is in place, the timer that would trigger it is not.

---

## D19 — Development ROMs are fetched, never committed

**Context.** The bundle shipped with the subject contains no CPU test, but the
subject names blargg's suite as legitimate test material (Ch. II, p.4).

**Decision.** `tools/fetch_dev_roms.sh` downloads them into `roms-dev/`, which
is gitignored. The repository keeps only the MIT bundle, which is the only ROM
material that will be evaluated (Ch. VII, p.11). The test script skips the
blargg layer cleanly when `roms-dev/` is absent, so a fresh clone still runs
its checks.

The CPU self-test compiled into the executable exists for the same reason: the
instruction set must remain verifiable with no external file at all.

---

## D20 — The link port is captured, not emulated

**Context.** The subject never mentions the serial port or a link cable, so
none is required.

**Decision.** Writes that start a transfer append the byte to a buffer the
emulator exposes. That is enough for blargg's ROMs to report their results,
which is how the entire instruction set was validated before any rendering
existed. No cable is simulated and no partner console is modelled.

---

## D21 — The disassembler shares the CPU's decoding shape

**Context.** Section V.1 (p.7) requires the debugger to display the next
instruction, which means a second decoder alongside the CPU's.

**Decision.** It extracts the same bit fields as the CPU (`x`, `y`, `z`, `p`,
`q`, decision D17) and indexes the same operand tables. The two stay
structurally parallel, so a change to one is visibly a change to the other.

It reads memory exclusively through `Bus::peek`, never `Bus::read`, so
disassembling costs no cycles and has no side effect (D15). The self-test
verifies that by disassembling 500 times and checking the clock did not move.

**Validation.** A disassembler is only trustworthy if its idea of an
instruction's length matches what the CPU consumes: one byte of disagreement
makes every following line of a listing wrong. Two checks:

- exhaustive, in `--cpucheck`: all 497 opcodes that advance sequentially
  (241 base plus 256 behind the CB prefix) are run and their length compared
  with PC's advance. Conditional branches are set up so they fall through.
  The fourteen unconditional jumps are checked by where they land or by the
  return address they push.
- empirical, in `--discheck`: real ROMs are executed and the same comparison
  is made on every non-branching instruction. Across the bundle plus blargg's
  suite, that is 52 million instruction lengths, covering 243 of the 256 base
  opcodes and all 256 CB ones. The thirteen never seen are the illegal
  opcodes, which no real ROM executes.

---

## D22 — "Run one frame" is defined in system cycles

**Context.** Section V.2 (p.7) asks the debugger to run "a single frame and/or
one second of emulation". The PPU does not exist yet (step 8), so there is no
VBlank signal to stop on.

**Decision.** A frame is 70224 system-domain cycles and a second is 4194304 of
them. Counting in the system domain rather than the CPU one means a frame
still lasts a frame in CGB double-speed mode. Step 8 will redefine it as "run
until the PPU signals VBlank"; the command and its meaning do not change.

Note the command reports slightly more than 70224 cycles, since emulation can
only stop on an instruction boundary.

---

## D23 — A GameBoy facade assembles the machine

**Context.** The frontend, the debugger and the headless runner all need a
Bus and a Cpu wired together and reset consistently.

**Decision.** `GameBoy` owns both and exposes `load`, `reset`, `step`,
`run_frame` and `run_seconds`. The wiring exists once. It is also where the
DMG/CGB choice is made, which is the hook the "forcing DMG/CGB" bonus
(Ch. VI, p.9) will need.

---

## D24 — The trace uses the community's reference format

**Context.** Differential tracing only works if the trace can be compared
against one produced by an emulator known to be correct.

**Decision.** `--trace` emits exactly the line format the community's
reference logs use:

```
A:01 F:B0 B:00 C:13 D:00 E:D8 H:01 L:4D SP:FFFE PC:0100 PCMEM:00,C3,50,01
```

Inventing a nicer format would have made the traces incomparable, which is the
whole point of producing them. `PCMEM`, the four bytes at PC, is what makes a
divergence readable without looking the address up in the ROM.

`--ly-stub` makes LY read 0x90, which those reference logs require: they were
recorded without a PPU. It is off by default and the emulator never uses it.

---

## D25 — Fingerprints guard against behavioural drift

**Context.** The reference logs themselves could not be downloaded from this
environment, so the comparison against them has to be run elsewhere. That left
the project without an automated guard on CPU behaviour.

**Decision.** `--tracehash` hashes the trace of a ROM's first 200000
instructions with FNV-1a, and `tests/expected/trace_digests.txt` stores one
digest per bundled ROM. Any change in what the CPU does moves a digest.

This is regression protection, not correctness proof: it says "behaviour
changed", not "behaviour is right". Correctness comes from blargg's suite
(step 4) and from comparing against reference logs.

Adding the timer, the PPU and the MBCs will legitimately move some digests.
The workflow is to regenerate with `--update` and read the diff, confirming
the change is the intended one rather than an accident.

---

## D26 — Tracing must not perturb the run

**Context.** A trace of a run that differs from the untraced run is worse than
no trace at all.

**Decision.** `trace_line` reads memory through `Bus::peek` only, so it
advances no clock and has no side effect (D15). The test suite verifies that
two runs of the same ROM produce byte-identical traces, and that a traced run
reaches the same state as an untraced one.

---

## D27 — The timer is modelled as one internal counter, not four registers

**Context.** DIV, TIMA, TMA and TAC look like four independent registers. They
are not, and every surprising behaviour the test bundle checks comes from that.

**Decision.** A single 16-bit counter increments on every cycle. DIV is simply
its upper byte, and TIMA counts the FALLING EDGES of one selected bit of that
same counter, chosen by TAC. Three consequences fall out for free rather than
needing special cases:

- writing to DIV resets the whole counter, it does not store the value;
- resetting DIV can increment TIMA, because clearing a watched bit that was
  set is a falling edge;
- changing TAC can do the same, for the same reason.

The overflow delay is modelled explicitly: when TIMA passes 0xFF it reads
0x00 for four cycles before TMA is copied in and the interrupt is requested.
A write to TIMA inside that window cancels both.

Implementing this any other way means bolting on one special case per quirk,
and missing the ones nobody documented.

---

## D28 — The timer follows the CPU clock, the PPU does not

**Context.** In CGB double-speed mode the CPU runs twice as fast.

**Decision.** `Bus::tick` feeds the timer CPU-domain cycles and the PPU
system-domain ones. DIV therefore does count twice as fast in double-speed
mode, which is what the hardware does, while the screen keeps refreshing
59.727 times per second. This is the separation decision D14 put in place at
step 3, now actually being used.

---

## D29 — Ticking one cycle at a time

**Context.** The falling-edge detector has to see every value the counter
takes, or an edge can be missed.

**Decision.** `Timer::tick` loops one cycle at a time rather than trying to
jump ahead analytically. It is the only implementation that cannot miss an
edge, and measurement says it costs nothing that matters: 60 emulated seconds
run in 1.4 real seconds, about 42 times faster than the hardware, against a
requirement of 1x (subject V.3, p.7).

Optimising this before measuring would have traded correctness for speed the
project does not need.

---

## D30 — mooneye verdicts are read from the registers

**Context.** The bundle's acceptance ROMs print their result on screen, which
needs a PPU, and do not use the link port.

**Decision.** `--mooneye` implements the convention those ROMs follow: the
registers hold 3, 5, 8, 13, 21, 34 on success and 0x42 everywhere on failure,
followed by `LD B,B` as a software breakpoint. `LD B,B` does nothing on real
hardware, which is why it was chosen as a marker.

That lets the two step-7 acceptance ROMs be validated now rather than waiting
for step 9, and the same command will read every other mooneye ROM later.
