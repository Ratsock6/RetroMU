# Journal des decisions techniques — RetroEmu

Ce fichier existe pour la soutenance. Chaque fois qu'une ambiguite du sujet
est tranchee, la decision est consignee ici avec ses alternatives, afin de
pouvoir la justifier devant un correcteur.

Reference : sujet **RetroEmu**, version 9.1, 12 pages.

---

## Ambiguites relevees dans le sujet

Le sujet ne precise pas les points suivants. Chacun a du etre tranche.

| # | Point non specifie | Ou |
|---|---|---|
| A1 | « Running thread » : un vrai thread est-il exige, ou seulement une GUI non degradee ? | V.3, p.7 |
| A2 | Les bibliotheques hors couche de rendu sont-elles libres ? (cas de Dear ImGui) | Ch. IV, p.6 |
| A3 | L'etat post-boot des registres releve-t-il de l'obligatoire ou du bonus ? | V.1 p.7 / Ch. VI p.9 |
| A4 | Quelle precision temporelle est exigee ? Aucun chiffre n'est donne. | V.3, p.7 |
| A5 | Des arguments en ligne de commande sont-ils autorises en plus de la GUI ? | Ch. IV, p.6 |
| A6 | Quelles erreurs faut-il gerer ? Aucune exigence ecrite. | — |
| A7 | Ou doit vivre le debugger (terminal, fenetre, overlay) ? | V.1, p.7 |

---

## D1 — SDL2 : strategie de build hybride

**Contexte.** Le sujet (Ch. IV, p.6) exige que le build « embarque ou documente
ses dependances runtime pour que le correcteur puisse lancer sans setup manuel ».

**Alternatives.**
1. `find_package(SDL2 REQUIRED)` + README « installez libsdl2-dev » — echoue si
   le correcteur n'a pas SDL2 : c'est precisement du setup manuel.
2. `FetchContent` systematique — fonctionne partout, mais recompile SDL2 meme
   quand le systeme le fournit deja.
3. **Hybride** : `find_package` d'abord, repli automatique sur `FetchContent`
   avec compilation statique.

**Decision : option 3.** Voir `cmake/SDL2Setup.cmake`.
En voie de repli, SDL2 est lie **statiquement** : l'executable produit ne
depend d'aucun `libSDL2.so`, ce qui satisfait litteralement le verbe
« bundle » du sujet.

**Verifie le 2026-09-16** sur un conteneur sans SDL2 installe.

---

## D2 — Les ROMs de test sont versionnees

**Contexte.** Le sujet (p.6, p.11) interdit les ROMs **commerciales** dans le
depot. Il demande (p.11) d'extraire `attachments/test-roms.zip` « a cote de
l'executable ».

**Decision.** Le bundle est commite dans `roms/`, fichiers `LICENSE` inclus
(les 9 ROMs sont sous licence MIT : Matt Currie pour acid2, Joonas Javanainen
pour mooneye). Le correcteur n'a donc rien a telecharger.
Les ROMs de developpement (Blargg, jeux personnels) vivent dans `roms-dev/`,
qui est gitignore.

---

## D3 — Propriete de la memoire : le Bus possede tout

**Alternatives.** Memoire globale ; pointeurs croises entre composants ;
un proprietaire unique.

**Decision.** `Bus` possede la cartouche, le PPU, le timer, le joypad, le DMA,
la WRAM et la HRAM. Le `Cpu` ne possede rien et recoit `Bus&` en parametre :
`u32 Cpu::step(Bus& bus);`
Pas de reference circulaire, chaque composant testable isolement.

---

## D4 — Le debugger vit dans le terminal

**Contexte (A2, A7).** Dear ImGui donnerait un plus bel affichage, mais le
sujet interdit « tout framework de plus haut niveau pour la couche de rendu »
(p.6) sans dire si ImGui en fait partie. Zone grise.

**Decision.** Debugger en terminal. Aucun risque en soutenance, testable par
script, et immediatement disponible. Un overlay dessine directement en SDL2
pourra etre ajoute a l'etape 15 si le temps le permet.

---

## D5 — Rendu du PPU a la scanline

**Alternatives.** Par frame (trop imprecis, echoue dmg-acid2) ; **par
scanline** ; par pixel avec FIFO materielle (exact, mais tres couteux).

**Decision.** Rendu par scanline, compose a la fin du mode 3.
Suffisant pour les deux ROMs acid2 du bundle. La FIFO n'est necessaire que
pour des effets mid-scanline qu'aucune ROM du bundle ne teste.

---

## D6 — Cadencage par horloge, pas par VSync

**Decision.** Pas de `SDL_RENDERER_PRESENTVSYNC`. Le VSync verrouillerait
l'emulation sur le taux de rafraichissement de l'ecran du correcteur (60 Hz),
alors que le materiel tourne a 59,727 Hz. Le rythme sera pilote par une
horloge haute resolution (etape 11).

---

## D7 — Boucle mono-thread

**Contexte (A1).** Le titre de la section V.3 dit « Running thread », mais le
corps du texte n'exige que « vitesse normale sans affecter negativement les
operations de la GUI » (p.7).

**Decision.** Boucle mono-thread : emuler une frame, puis traiter les
evenements, puis afficher, puis attendre. L'exigence ecrite est satisfaite.
SDL2 impose de toute facon de pomper les evenements sur le thread proprietaire
de la fenetre, ce qui rend le multithreading plus risque qu'utile ici.

**A defendre en soutenance** si un correcteur lit le titre de section de
maniere litterale.

---

## D8 — Timing au M-cycle, par « tick-on-access » ⚫

**La decision la plus structurante du projet.**

**Contexte (A4).** Le sujet ne chiffre aucune precision temporelle. Mais le
bundle de ROMs impose de fait le niveau requis : il contient
`mooneye/acceptance/div_timing.gb`, `intr_timing.gb` et `oam_dma/basic.gb`,
qui verifient a quel cycle exact, **a l'interieur d'une instruction**, le
registre DIV s'incremente et le drapeau IF est echantillonne.

**Alternatives.**
1. Rattrapage en fin d'instruction (`n = cpu.step(); ppu.tick(n);`) — simple,
   mais ne peut pas passer ces trois ROMs.
2. **Tick-on-access** : chaque acces memoire fait avancer l'horloge de 4
   cycles au moment ou il a lieu.
3. FIFO au T-cycle — exact, hors de proportion pour le bundle.

**Decision : option 2.**
```cpp
u8 Bus::read(u16 addr) { tick(4); return dispatch(addr); }
```
Corollaire : le temps est compte dans deux unites distinctes des le depart,
`t_cpu` et `t_sys`, car en mode double vitesse CGB (section V.6, p.8) le CPU
tourne deux fois plus vite mais **pas** le PPU.

Poser cette separation au depart coute trois lignes ; la rajouter apres coup
imposerait de reecrire le CPU, le PPU, le timer et le DMA.

---

## D9 — Arguments en ligne de commande en plus de la GUI

**Contexte (A5).** Le sujet exige une GUI avec « load, play, pause » (p.6) et
ne dit rien d'une interface en ligne de commande.

**Decision.** La GUI reste le moyen de chargement exige. Un argument optionnel
`./retroemu [rom]` est accepte en plus : il n'enleve rien a l'exigence et
accelere enormement le cycle de test pendant le developpement.

Un mode `--selftest` sans fenetre est egalement fourni, pour pouvoir valider
la chaine graphique en SSH ou en conteneur.

---

## D10 — Langue du code

**Contexte.** Le sujet est explicite : « **No coding style is enforced.** You
may follow any convention you like as long as your code remains readable to
your peer evaluators. » (p.6). Aucune contrainte de langue n'existe.

**Decision.** Identifiants en anglais (ils reprennent la terminologie du
materiel : `LCDC`, `SCX`, `OAM`), commentaires en francais (les correcteurs
sont francophones, et le code doit pouvoir etre defendu).
