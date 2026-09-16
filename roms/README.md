# Test ROMs

Official bundle shipped with the subject (`attachments/test-roms.zip`),
extracted here and versioned. The subject (Chapter VII, p.11) states that this
is **the canonical test material used during evaluation** and the only ROM
material that will be evaluated.

All 9 ROMs are **MIT-licensed**, so keeping them in the repository is
legitimate. See `acid2/LICENSE` (Matt Currie) and `mooneye/LICENSE`
(Joonas Javanainen).

> The subject forbids any **commercial** ROM in the repository (p.6 and p.11).
> Development ROMs (Blargg, personally owned dumps) belong in `roms-dev/`,
> which is gitignored.

## What each ROM validates

| ROM | Header | Plan step | What is tested |
|---|---|---|---|
| `mooneye/acceptance/div_timing.gb` | ROM ONLY, 32 KiB | 7 | DIV register timing |
| `mooneye/acceptance/intr_timing.gb` | ROM ONLY, 32 KiB | 7 | Interrupt service timing |
| `mooneye/acceptance/oam_dma/basic.gb` | ROM ONLY, 32 KiB | 10 | OAM DMA basics |
| `acid2/dmg-acid2.gb` | ROM ONLY, 32 KiB | 9 | DMG PPU: background, window, sprites, priorities |
| `mooneye/mbc1/rom_512kb.gb` | MBC1, 64 KiB | 13 | ROM bank switching (4 banks) |
| `mooneye/mbc1/ram_64kb.gb` | MBC1+RAM+BATTERY, 8 KiB RAM | 13 | MBC1 RAM + battery save |
| `mooneye/mbc2/ram.gb` | MBC2+BATTERY, 32 KiB | 13 | Built-in 4-bit RAM |
| `mooneye/mbc5/rom_2Mb.gb` | MBC5, 256 KiB | 13 | ROM bank switching (16 banks) |
| `acid2/cgb-acid2.gbc` | ROM ONLY, **CGB ONLY** | 14 | CGB PPU: palettes, tile attributes, VRAM bank 1 |

**Careful with the file names**: they are in **kilobits**, not kilobytes.
`rom_512kb` = 512 Kbit = **64 KiB**. `rom_2Mb` = 2 Mbit = **256 KiB**.
`ram_64kb` = 64 Kbit = **8 KiB**.

## How to read the results

- **Mooneye** prints `Test OK` or `TEST FAILED` directly on screen (and, for
  the MBC tests, `BANK NUMBER` / `EXPECTED` / `ACTUAL`). No serial port is
  needed.
- **acid2** draws a face. Every defect in the face points at a specific PPU
  bug; the author provides a reference image to compare against pixel by
  pixel.
