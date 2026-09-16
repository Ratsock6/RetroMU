# RetroEmu

Emulateur de console portable 8 bits — modele **DMG** (1989) et son successeur
couleur **CGB** (1998).

Projet 42 — sujet *RetroEmu*, version 9.1.

---

## Compilation

**Une seule commande**, comme exige par le sujet (Chapitre IV, p.6) :

```bash
cmake -B build && cmake --build build -j
```

L'executable est alors `./build/retroemu`.

### Dependances

La seule dependance est **SDL2**, imposee par le sujet pour les couches
graphique, entrees et audio (p.6).

**Aucune installation manuelle n'est requise.** Le build detecte SDL2 :

- s'il est present sur le systeme, il est utilise tel quel (build rapide) ;
- sinon, CMake telecharge SDL2 et le compile **en statique**. L'executable
  produit embarque alors SDL2 et ne depend d'aucun `libSDL2.so`.

La premiere compilation dure quelques minutes dans le second cas ; les
suivantes sont mises en cache.

Prerequis reels : un compilateur C++17 (GCC 7+ ou Clang 6+), CMake 3.16+, et
`git` si SDL2 doit etre telecharge.

### Options de build

| Option | Defaut | Effet |
|---|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | `Release` | Build de debogage (non optimise) |
| `-DRETROEMU_SANITIZE=ON` | `OFF` | Active AddressSanitizer et UBSan |
| `-DRETROEMU_FORCE_FETCH_SDL2=ON` | `OFF` | Ignore le SDL2 du systeme et le recompile |
| `-DRETROEMU_WARNINGS=OFF` | `ON` | Desactive les avertissements stricts |

---

## Utilisation

```bash
./build/retroemu                      # ouvre la fenetre
./build/retroemu --scale 6            # fenetre agrandie x6
./build/retroemu --selftest           # verifie la chaine graphique, sans fenetre
./build/retroemu --selftest out.ppm   # ... et sauvegarde l'image produite
./build/retroemu --help
```

Le mode `--selftest` n'ouvre aucune fenetre : il permet de valider le build en
SSH, en conteneur, ou partout ou il n'y a pas de serveur graphique.

---

## Etat d'avancement

Le projet est developpe par etapes, chacune validee par une ROM du bundle de
test avant de passer a la suivante.

| # | Etape | Etat |
|---|---|---|
| 0 | Mise a niveau hexadecimal / bit a bit | ✅ |
| 1 | Squelette CMake + SDL2 + fenetre 160x144 | ✅ |
| 2 | Cartouche : chargement ROM et parsing de l'en-tete | ⏳ |
| 3 | Bus / MMU avec tick-on-access | ⏳ |
| 4 | CPU : registres, flags, jeu d'instructions | ⏳ |
| 5 | Desassembleur et debugger *(sujet V.1)* | ⏳ |
| 6 | Log de trace et validation differentielle | ⏳ |
| 7 | Interruptions et timer | ⏳ |
| 8 | PPU : machine a etats | ⏳ |
| 9 | PPU : background, window, sprites, palettes *(sujet V.2)* | ⏳ |
| 10 | DMA vers l'OAM | ⏳ |
| 11 | Boucle temps reel et entrees *(sujet V.3, V.4)* | ⏳ |
| 12 | GUI : load / play / pause *(sujet Ch. IV)* | ⏳ |
| 13 | MBC1, MBC2, MBC5 et sauvegarde par pile *(sujet V.5)* | ⏳ |
| 14 | CGB : palettes, banque VRAM, HDMA, double vitesse *(sujet V.6)* | ⏳ |
| 15 | Robustesse et finalisation | ⏳ |

---

## Organisation du depot

```
RetroEmu/
├── CMakeLists.txt          build principal
├── cmake/SDL2Setup.cmake   detection SDL2 avec repli automatique
├── include/retroemu/       en-tetes
│   ├── core/               CPU, bus, PPU, timer, cartouche...
│   ├── debug/              desassembleur, tracer, debugger
│   └── front/              SDL2, interface
├── src/                    implementations
├── roms/                   bundle de test MIT (versionne)
├── roms-dev/               ROMs de developpement (gitignore)
├── tests/                  tests unitaires et scripts
└── docs/
    ├── decisions.md        journal des decisions techniques
    └── etape0/bitwise.cpp  terrain d'entrainement hexa / bit a bit
```

---

## ROMs

`roms/` contient le bundle de test fourni avec le sujet : 9 ROMs sous licence
MIT (acid2 de Matt Currie, mooneye de Joonas Javanainen). Voir
`roms/README.md` pour le detail de ce que chacune valide.

**Aucune ROM commerciale ne se trouve dans ce depot**, conformement au sujet
(p.6 et p.11).

---

## Documentation technique

Le materiel emule est documente par la communaute homebrew, a laquelle le
sujet renvoie explicitement (p.4) : **Pan Docs** et le **GBDev wiki**.

Les choix techniques du projet, ainsi que les points laisses ambigus par le
sujet, sont consignes dans [`docs/decisions.md`](docs/decisions.md).
