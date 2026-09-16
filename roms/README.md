# ROMs de test

Bundle officiel fourni avec le sujet (`attachments/test-roms.zip`), extrait ici
et versionne. Le sujet (Chapitre VII, p.11) indique que c'est **le materiel de
test canonique utilise pendant l'evaluation** et le seul qui sera evalue.

Les 9 ROMs sont sous **licence MIT** : leur presence dans le depot est
legitime. Voir `acid2/LICENSE` (Matt Currie) et `mooneye/LICENSE`
(Joonas Javanainen).

> Le sujet interdit toute ROM **commerciale** dans le depot (p.6 et p.11).
> Les ROMs de developpement (Blargg, jeux personnels) vont dans `roms-dev/`,
> qui est gitignore.

## Ce que chaque ROM valide

| ROM | En-tete | Etape du plan | Ce qui est teste |
|---|---|---|---|
| `mooneye/acceptance/div_timing.gb` | ROM ONLY, 32 Kio | 7 | Timing du registre DIV |
| `mooneye/acceptance/intr_timing.gb` | ROM ONLY, 32 Kio | 7 | Timing du service d'interruption |
| `mooneye/acceptance/oam_dma/basic.gb` | ROM ONLY, 32 Kio | 10 | Bases du DMA vers l'OAM |
| `acid2/dmg-acid2.gb` | ROM ONLY, 32 Kio | 9 | PPU DMG : background, window, sprites, priorites |
| `mooneye/mbc1/rom_512kb.gb` | MBC1, 64 Kio | 13 | Commutation de banques ROM (4 banques) |
| `mooneye/mbc1/ram_64kb.gb` | MBC1+RAM+BATTERY, 8 Kio RAM | 13 | RAM MBC1 + sauvegarde par pile |
| `mooneye/mbc2/ram.gb` | MBC2+BATTERY, 32 Kio | 13 | RAM 4 bits integree |
| `mooneye/mbc5/rom_2Mb.gb` | MBC5, 256 Kio | 13 | Commutation de banques ROM (16 banques) |
| `acid2/cgb-acid2.gbc` | ROM ONLY, **CGB ONLY** | 14 | PPU CGB : palettes, attributs, banque VRAM 1 |

**Attention aux noms** : ils sont en **kilobits**, pas en kilo-octets.
`rom_512kb` = 512 Kbit = **64 Kio**. `rom_2Mb` = 2 Mbit = **256 Kio**.
`ram_64kb` = 64 Kbit = **8 Kio**.

## Comment lire les resultats

- **Mooneye** affiche `Test OK` ou `TEST FAILED` directement a l'ecran (avec,
  pour les tests MBC, `BANK NUMBER` / `EXPECTED` / `ACTUAL`). Aucun port serie
  n'est necessaire.
- **acid2** dessine un visage. Chaque defaut du visage designe un bug precis du
  PPU ; l'auteur fournit une image de reference a comparer pixel par pixel.
