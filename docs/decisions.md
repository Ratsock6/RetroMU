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

---

## D31 — The PPU sweeps line by line from the start

**Context.** Step 8 could have been skipped: a renderer that draws the whole
image once per frame would produce something on screen sooner.

**Decision.** The mode 2 / 3 / 0 sweep and the LY counter are implemented
first, before any pixel. Two reasons, both structural rather than cosmetic:

- LY is what games synchronise on. dmg-acid2 spins on `LDH A,($44)` /
  `CP $90` / `JR NZ,-6` until LY reaches 144, which the step 6 tracer located
  at instruction 12. Without the sweep, nothing runs far enough to draw.
- VBlank is the only window where writing video memory is safe, so games do
  all their work inside the interrupt it raises. No VBlank means no game.

The sweep also gives step 9 the place to hook scanline rendering (D5) without
restructuring anything.

---

## D32 — The STAT interrupt fires on a rising edge, not on each event

**Context.** STAT can be told to interrupt on four different conditions.

**Decision.** All enabled sources are OR-ed into one internal line, and only a
0-to-1 transition raises the interrupt. Two conditions overlapping therefore
give one interrupt, not two.

A consequence worth knowing, and now covered by a test: enabling a source
whose condition already holds fires immediately, because that too is a rising
edge. It bit the test suite before it was understood.

---

## D33 — VRAM and OAM blocking is deliberately not implemented

**Context.** On real hardware VRAM is unreadable during mode 3 and OAM during
modes 2 and 3; reads return 0xFF and writes are dropped.

**Decision.** Not implemented for now. No ROM in the subject's bundle tests
it, and being permissive can only make a well-behaved game work, never break
one: games already confine their video writes to VBlank precisely because of
the blocking. Implementing it with mode timings that are not yet exact would
be the riskier choice.

To revisit if a test ever demands it, in which case mode 3's variable length
needs to be modelled first.

---

## D34 — A frame ends when the PPU says so

**Context.** Until now `run_frame` advanced a fixed 70224 system cycles (D22).

**Decision.** It now runs until the PPU signals a completed frame, which
happens when LY reaches 144 and the image is finished, at 65664 cycles into
the frame. A cycle cap remains as a guard, because a game that turns the
screen off signals nothing and would otherwise never return.

That last case is real and visible: mooneye's ROMs and dmg-acid2 both turn the
screen off while they rewrite video memory, which is why one emulated second
draws about 52 images rather than 59 for those ROMs. The hardware's 59.727
frames per second is verified independently of any ROM inside --cpucheck.

---

## D35 — Scanline rendering, composed at the end of mode 3

**Context.** Decision D5 chose scanline rendering over a per-frame renderer
(too coarse) and a per-pixel FIFO (exact, but far more work).

**Decision.** Each visible line is composed in one pass when mode 3 ends: the
background and window first, into a buffer that also keeps the raw colour
index, then the sprites on top. Keeping the raw index is what makes the
sprite priority rule expressible, since a sprite marked "behind the
background" still shows over background colour 0.

It is enough for the whole bundle. A FIFO would only be needed for effects
that change registers in the middle of a line, which no bundle ROM does.

---

## D36 — The framebuffer holds colours, not shade indices

**Context.** A DMG pixel is one of four shades; a CGB pixel is a 15-bit
colour. The framebuffer has to serve both.

**Decision.** It holds ARGB8888 throughout, and the PPU maps DMG shades
through a fixed four-colour table. The frontend therefore never has to know
which model is running, and step 14 fills the same buffer with real colours
without changing anything downstream.

The greenish palette is a display choice, not hardware: the console's four
shades are whatever its LCD showed. Isolating it in one table leaves room for
the UX bonus to offer alternatives.

---

## D37 — The reference image is committed, converted to PPM

**Context.** dmg-acid2 is the canonical PPU test: it draws a face, and every
feature it exercises has a documented failure mode, so a defect in the
picture points straight at the bug behind it. Validating against it needs the
author's reference image.

**Decision.** `tests/expected/dmg-acid2-reference.ppm` holds it, converted
once from the original 2-bit greyscale PNG into the project's own palette. It
is MIT-licensed by the same author as the acid2 ROMs already in `roms/`, and
the licence travels with it.

Converting to PPM rather than keeping the PNG makes the comparison a plain
byte compare: no network, no image library, no Python, nothing for a corrector
to install.

Result: 23040 pixels, zero differences.

---

## D38 — Two ways to look at the picture

**Context.** A rendering bug is invisible in a trace and produces no error.

**Decision.** Two tools, deliberately at different levels:

- `--tiles` dumps the 384 tiles in video memory. It checks the building blocks
  before the assembly: if the tiles are noise, the 2-bit decoding is wrong and
  nothing downstream can be right. On dmg-acid2 it shows a legible character
  set, which settled that question in one look.
- `--screenshot` captures the assembled screen, stopping on the ROM's own
  `LD B,B` marker so the image is stable rather than caught mid-draw.

A side benefit: the mooneye ROMs print their results on screen, so their
messages are now readable. `rom_2Mb` shows "TEST FAILED / BANK NUMBER /
EXPECTED / ACTUAL", which will make step 13 far easier to debug than a bare
pass or fail.

---

## D39 — The copier reads through peek and poke, never read and write

**Context.** `Bus::tick` drives the OAM copier, and the copier moves a byte by
reading from memory and writing into the sprite table.

**Decision.** It uses `peek` and `poke`. Using `read` and `write` would charge
the clock four more cycles for an access the very tick already accounted for,
and the copier would drive itself: each byte would advance the clock, which
would tick the copier, which would move another byte.

The same reasoning as decision D15, arriving from the opposite direction: peek
exists because inspecting must not perturb, and here because a component the
clock drives must not drive the clock back.

---

## D40 — The bus block applies to the CPU only

**Context.** While the copier runs it owns the bus, and on real hardware the
CPU can only reach HRAM. That is why games copy their DMA routine into HRAM
and run it from there: anywhere else their own instruction fetches would be
blocked.

**Decision.** The restriction lives in `Bus::read` and `Bus::write`, the two
accessors the CPU uses, and not in `dispatch_read` / `dispatch_write`. So
`peek` and `poke` stay clear of it, which is what lets the copier move its own
bytes and the debugger and tracer look at memory while a transfer is running.

Implementing the block was a judgement call, since no bundle ROM demands it
and being permissive can only help a well-behaved game. It was kept because
it costs one predicate, it is what the hardware does, and it was verified not
to change anything: all three acceptance ROMs still pass and dmg-acid2 still
matches its reference pixel for pixel with the block in place.

---

## D41 — The copier follows the CPU clock

**Context.** The copier moves one byte per machine cycle.

**Decision.** `Bus::tick` feeds it CPU-domain cycles, like the timer and
unlike the PPU (D28). In CGB double-speed mode the machine cycles are shorter,
so the transfer finishes in half the real time, which is what the hardware
does.

---

## D42 — The keyboard mapping is a documented choice

**Context.** Section V.4 (p.7) names the buttons but says nothing about which
keys reach them.

**Decision.** Arrow keys for the control pad, `X` for A, `Z` for B, `Enter`
for Start, `Backspace` or right `Shift` for Select, following the layout most
emulators use. It lives in one table at the top of `src/front/frontend.cpp`,
so changing it is a one-place edit and the UX bonus (Ch. VI, p.9) could make
it configurable.

`Space` pauses and `Escape` quits. Those are not buttons of the console; they
are the keyboard route to the play and pause that section IV (p.6) requires
of the GUI, which step 12 will also expose as controls.

---

## D43 — Pacing by deadline, with a hybrid wait

**Context.** Section V.3 (p.7) requires normal speed without degrading the
interface. Decision D6 already ruled out VSync, which would tie emulation to
whatever refresh rate the corrector's monitor happens to have, while the
hardware runs at 59.727 frames per second.

**Decision.** A deadline is computed from the hardware's own figures
(70224 system cycles at 4194304 Hz) and waited for. `SDL_Delay` is coarse, a
millisecond at best, so it covers the bulk of the wait and a short spin covers
the tail; spinning the whole way would burn a core for nothing.

When a frame finishes late the deadline restarts from the present instead of
accumulating a debt, which would otherwise make the emulator sprint to catch
up after any hiccup. Late frames are counted and reported.

Measured: 180 frames in 3021 ms against the 3014 ms the hardware would take,
with zero late frames.

---

## D44 — The renderer falls back to software

**Context.** `SDL_CreateRenderer` with `SDL_RENDERER_ACCELERATED` fails
outright when no GPU driver is available.

**Decision.** Accelerated first, then software. A corrector running over SSH
with X forwarding, inside a virtual machine, or on a system with no GPU driver
has no accelerated renderer, and refusing to start there would be a poor
reason to fail an evaluation. The fallback prints one line saying what
happened.

Found while trying to test the loop under SDL's dummy video driver, which
provides no accelerated renderer either. That the test environment and a
plausible correction environment fail the same way is the point.

---

## D45 — The frontend is the only place that includes SDL

**Context.** Section IV (p.6) imposes SDL2 for graphics, input and audio.

**Decision.** `src/front/frontend.cpp` is the only translation unit that
includes it. Everything under `core/` produces a 160x144 buffer of colours and
consumes eight button states, and knows nothing about how either reaches a
human.

That separation is what lets the whole emulator be tested with no window at
all: `--screenshot`, `--mooneye`, `--cpucheck` and the debugger all drive the
same core. The window is one more consumer, not the program.

---

## D46 — The interface is drawn by hand, with SDL2 only

**Context.** Chapter IV of the subject (p.6) requires a GUI with at least
load, play and pause, and forbids any higher-level framework for the rendering
layer. Decision D4 already kept Dear ImGui out of the project to stay clear of
that grey area, at a cost: SDL2 draws rectangles and textures and knows
nothing about text.

**Decision.** The interface is rectangles and a bitmap font blitted from a
texture built at startup. A control bar sits BELOW the screen rather than over
it, so nothing the game draws is ever hidden by the interface.

Deliberately plain. It has to be unmistakable to a corrector, not pretty.

---

## D47 — The font is public domain, and embedded

**Context.** Text needs glyphs, and drawing 95 of them by hand is slow and
error-prone.

**Decision.** `include/retroemu/front/font.hpp` holds the printable ASCII
range of Daniel Hepper's font8x8, which is public domain and itself derived
from the public domain IBM VGA fonts. The provenance and licence are recorded
in the file's own header.

Note its encoding is the opposite of the console's: here the LEAST significant
bit of a byte is the LEFTMOST pixel. Mixing the two up would have produced
mirrored glyphs, which is exactly why the font was rendered and looked at
before anything was built on top of it.

---

## D48 — Load means a browser, not only a command-line argument

**Context.** A ROM path on the command line is not a GUI. The subject's
requirement is a failure criterion, so it has to be satisfied inside the
window.

**Decision.** The Load button opens a browser listing directories and
`.gb` / `.gbc` files, walkable with the keyboard or the mouse. Everything else
is filtered out: it exists to find a cartridge, not to explore a disk. The
emulator can be started with no argument at all and still reach a cartridge.

Drag and drop loads as well, which the subject lists as a UX bonus example
(Ch. VI, p.9) and which gives a second route to the same one place in the code
that loads a cartridge, so all three behave identically.

---

## D49 — Key presses can be scripted, so the GUI is testable

**Context.** An interface nobody can test is an interface that quietly breaks.
The rest of the project is covered by scripts; the GUI was about to be the
exception.

**Decision.** `--ui-keys up,down,return` pushes key presses into the event
queue, one per frame, and `--out` writes the whole window, interface included,
to a PPM. With SDL's dummy video driver both work with no display attached.

That is what lets the test suite prove the subject's requirement rather than
assert it: it walks the browser into a directory, back out, onto a cartridge,
and checks the cartridge was loaded. Fifteen checks drive the interface
exactly as a human would.

---

## D50 — The controller owns the ROM and the RAM

**Context.** A `Cartridge` is moved into the `Bus` when it is attached. If the
controller held references into the cartridge's buffers, the move would leave
it pointing at freed memory.

**Decision.** `Mbc` owns the ROM and the cartridge RAM outright, and
`Cartridge` holds a `unique_ptr<Mbc>`. Moving a cartridge moves the pointer
and the data travels with it. `Cartridge` becomes move-only, which nothing in
the project needed it not to be.

Three controllers plus one for cartridges with no chip at all, behind one
interface, so the bus has a single path: everything in 0x0000-0x7FFF goes to
the controller, which decides which bank each window shows.

---

## D51 — Bank numbers wrap rather than reading past the end

**Context.** A game may select a bank that does not exist on the cartridge it
is running from.

**Decision.** The bank number is taken modulo the number of banks that exist,
for ROM and for RAM alike. That is not a defensive check: it is what the
hardware does, because the address lines that would carry the extra bits are
simply not connected, so they read back as whatever the connected ones say.

Getting this wrong for RAM is what made mooneye's `mbc1/ram_64kb` fail at
round 3. With a single 8 KiB chip there are no wires for the bank bits, so
selecting bank 1 must show bank 0 again; returning 0xFF instead looked
plausible and was wrong.

---

## D52 — Each controller's quirks, kept rather than smoothed over

**MBC1.** Writing 0 to the five-bit register selects bank 1, not bank 0, so
bank 0 cannot be placed in the switchable window at all, and on a large
cartridge banks 0x20, 0x40 and 0x60 are unreachable there too. The mode bit
changes what the second register means, and in mode 1 it reaches the fixed
window as well.

**MBC2.** Its RAM is inside the chip: 512 half-bytes, so only the low four
bits of each cell exist and reads return the rest as ones. The header
announces no RAM at all. And it has no separate address range for its two
commands: bit 8 of the ADDRESS written to decides which one is meant.

**MBC5.** Nine bits of bank number across two registers, and unlike MBC1 bank
0 can be selected: writing 0 means bank 0.

Each of these looks like a bug to smooth over and is in fact what the
cartridge does. Games depend on them.

---

## D53 — When to save is the caller's business

**Context.** Section V.5 (p.8) requires battery-backed RAM "persisted on disk
between sessions".

**Decision.** `Cartridge::load_battery` runs automatically when a cartridge is
loaded, because the whole point of a battery is that the game finds its data
where it left it. `save_battery` has to be asked for: the window asks on exit
and before loading another cartridge, the debugger on quit and on the `save`
command.

Two conditions before a file is written: the cartridge must have a battery,
and the game must actually have written to the RAM. A cartridge that was
merely looked at leaves nothing behind, so running the test suite does not
litter `roms/` with save files.

The save sits next to the ROM with its extension replaced, and only a dot in
the final path component counts as an extension, so a directory named
`my.roms` does not truncate the path.

---

## D54 — The colour renderer is the same code path, not a second one

**Context.** Section V.6 (p.8) requires Game Boy Color support: palettes,
the extra VRAM bank, tile attributes, HDMA and double speed.

**Decision.** There is ONE background renderer and ONE sprite renderer. The
CGB additions enter through a single byte — the tile's attribute — which is
read from VRAM bank 1 on a colour machine and is a hard zero everywhere else:

```cpp
const u8 attr = cgb_ ? vram_byte(1, map_addr) : 0;
```

Every attribute test downstream (flip, bank, palette, priority) then falls
through to the black-and-white behaviour on a DMG without a single `if (cgb)`
around it. Only three places genuinely differ and are marked as such: which
palette table a colour index is looked up in, what LCDC bit 0 means, and how
two overlapping sprites are ranked.

A second `render_background_cgb()` would have been easier to write and would
have doubled the surface where a bug can hide: dmg-acid2 would no longer
defend the colour path, nor cgb-acid2 the black-and-white one.

---

## D55 — Five bits to eight: `(v << 3) | (v >> 2)`

**Context.** A CGB colour is 15 bits, five per channel. A screen capture is
24 bits, eight per channel. The conversion has to be chosen.

**Decision.** `(v << 3) | (v >> 2)`: the top three bits are copied into the
bottom three, so 31 maps to 255 and 0 maps to 0.

The obvious `v << 3` is wrong in a way that is invisible by eye: white would
come out as 248,248,248 instead of 255,255,255, and EVERY pixel of a capture
would be slightly off. It is also the formula the cgb-acid2 author documents
for automated comparison, which is what lets `tests/run_cgb_tests.sh` use a
plain `cmp` instead of a per-pixel tolerance.

Real hardware also applies a colour-correction curve — the console's screen is
dim, so games are authored bright. Emulating it would make captures prettier
and the reference comparison impossible. It is not applied.

---

## D56 — LCDC bit 0 means two different things, so it is tested twice

**Context.** On a DMG, clearing bit 0 of LCDC blanks the background and the
window. On a CGB the same bit means "the priority bits decide who covers
whom"; the background never disappears.

**Decision.** Both meanings live in the code, selected by `cgb_`, and both
have their own check in `--cpucheck`:

- *LCDC bit 0 blanks the background* (DMG)
- *on a CGB, LCDC bit 0 does not blank the background*

The second one would pass by accident if the first were simply deleted, which
is why the pair is kept rather than one general check.

This bit is why the sprite pass receives the background's ATTRIBUTE bytes as
well as its colour indices: a pixel is covered by the background when the
sprite asks for it (its own bit 7), or when the tile asks for it (attribute
bit 7), unless master priority overrules both.

---

## D57 — The VRAM copier lives on the bus, not in the PPU

**Context.** The CGB adds a second DMA engine (0xFF51-0xFF55) which moves up
to 2 KiB into video memory, either all at once or 16 bytes per HBlank.

**Decision.** It is implemented in `Bus`, like the OAM copier of step 10,
because its SOURCE can be anywhere — ROM, external RAM, work RAM — and only
the bus can reach all of that. The PPU's only contribution is a single signal,
`take_hblank_entered()`, which the bus drains on every tick.

It moves bytes with `peek`/`poke`, never `read`/`write`: the copier is not the
CPU, so charging its accesses to the clock would count the same time twice
(decision D15). The time the CPU loses to a general-purpose transfer is
charged once, explicitly, by the bus.

That same rule forced a small change: `poke` now passes `timed = false` down
the write dispatch, so that poking 0xFF55 from the debugger performs the
transfer without advancing the clock. `peek` and `poke` have been free of
side effects on time since step 5, and the new register does not get to be an
exception.

---

## D58 — A colour console running a black-and-white cartridge stays in black and white

**Context.** `--cgb` can force the colour machine, and a real CGB happily runs
DMG cartridges.

**Decision.** The model comes from the cartridge header; `--cgb` overrides it.
But a CGB running a cartridge that declares no colour support renders through
the DMG path, with the four classic shades. All the extra hardware is still
there — the second VRAM bank, the palette registers, the VRAM copier, the
speed switch — it is simply not used by a game that does not know about it.

The test that proves it is the strongest kind available: `dmg-acid2.gb` forced
onto a CGB produces a capture byte-for-byte identical to the DMG reference
image.

A real CGB boot ROM goes one step further and colourises some known DMG games
from a table indexed by a hash of the cartridge title. That is a boot-ROM
feature, not a PPU feature, and the mandatory part skips the boot ROM
(ambiguity A3), so it is not implemented.

---

## D59 — STOP is two instructions wearing one opcode

**Context.** A CGB game switches to double speed by writing 1 to KEY1
(0xFF4D) and then executing STOP.

**Decision.** The CPU asks the bus whether a switch is armed. If it is, the
console does not stop: the clock changes speed, the divider is reset, about
2050 machine cycles are charged for the pause the hardware takes, and
execution continues at the next instruction.

Getting this wrong is not subtle — a game that uses double speed freezes on
its first frame — but it is easy to miss, because STOP is a one-line opcode
that looks finished long before step 14.

This is also the moment the two clock domains introduced in step 3 (decision
D14) finally pay for themselves. `Clock::advance` already returns system
cycles, so nothing else in the emulator had to change: the CPU, the timer and
the sprite copier speed up, and the PPU keeps refreshing the screen 59.727
times a second because it is fed from the other domain.
