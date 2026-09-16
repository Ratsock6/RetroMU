// ===========================================================================
//  RetroEmu — Etape 0 : mise a niveau hexadecimal / binaire / bit a bit
// ===========================================================================
//
//  Ce fichier NE FAIT PAS PARTIE de l'emulateur. C'est un terrain
//  d'entrainement. Il est isole dans docs/ et n'est pas compile par CMake.
//
//  Compilation :
//      g++ -std=c++17 -Wall -Wextra -o /tmp/bitwise docs/etape0/bitwise.cpp
//      /tmp/bitwise
//
//  Objectif : rendre automatiques les 6 gestes que tu repeteras des milliers
//  de fois dans ce projet. Tant que la section 6 (exercices) n'est pas
//  evidente pour toi, ne passe pas a l'etape 3 (le Bus).
// ===========================================================================

#include <cstdint>
#include <cstdio>
#include <string>

// --- Outil d'affichage : montre un octet en binaire, groupe par quartets ---
static std::string bin8(std::uint8_t v)
{
    std::string s;
    for (int bit = 7; bit >= 0; --bit) {
        s += ((v >> bit) & 1) ? '1' : '0';
        if (bit == 4) s += ' ';          // separe les 2 quartets : 0011 1100
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

static void titre(const char *t)
{
    std::printf("\n\033[1;36m=== %s ===\033[0m\n", t);
}

// ===========================================================================
//  1. HEXADECIMAL : pourquoi il est partout dans ce projet
// ===========================================================================
static void section_hexa()
{
    titre("1. HEXADECIMAL");
    std::puts("Un chiffre hexa = exactement 4 bits (un quartet). C'est LA raison\n"
              "pour laquelle tout le materiel se decrit en hexa : la conversion\n"
              "hexa <-> binaire est mecanique, chiffre par chiffre.\n");

    std::printf("  %-8s %-12s %s\n", "hexa", "binaire", "decimal");
    std::printf("  %-8s %-12s %s\n", "----", "-------", "-------");
    const std::uint8_t table[] = {0x00, 0x01, 0x0F, 0x10, 0x3C, 0x80, 0xA5, 0xFF};
    for (std::uint8_t v : table)
        std::printf("  0x%02X     %-12s %u\n", v, bin8(v).c_str(), v);

    std::puts("\n  A retenir :  0x0F = 0000 1111  (quartet bas plein)");
    std::puts("               0xF0 = 1111 0000  (quartet haut plein)");
    std::puts("               0xFF = 1111 1111  (255, la valeur max d'un octet)");
    std::puts("               0x80 = 1000 0000  (seul le bit 7 est a 1)");
}

// ===========================================================================
//  2. LES 6 GESTES FONDAMENTAUX
// ===========================================================================
static void section_operations()
{
    titre("2. LES 6 GESTES FONDAMENTAUX");

    const std::uint8_t a = 0x3C;   // 0011 1100
    const std::uint8_t b = 0x0F;   // 0000 1111

    std::printf("  a    = 0x%02X = %s\n", a, bin8(a).c_str());
    std::printf("  b    = 0x%02X = %s\n\n", b, bin8(b).c_str());

    // ET : garde un bit seulement si les DEUX sont a 1  -> sert a MASQUER
    std::printf("  a & b  = 0x%02X = %s   ET  : ne garde que les bits communs (MASQUER)\n",
                a & b, bin8(a & b).c_str());

    // OU : garde le bit si AU MOINS UN est a 1  -> sert a POSER des bits
    std::printf("  a | b  = 0x%02X = %s   OU  : allume des bits (POSER)\n",
                a | b, bin8(a | b).c_str());

    // OU EXCLUSIF : 1 si les deux different  -> sert a INVERSER des bits
    std::printf("  a ^ b  = 0x%02X = %s   XOR : inverse les bits selectionnes\n",
                a ^ b, bin8(a ^ b).c_str());

    // NON : inverse tout  -> sert a construire un masque d'effacement
    std::printf("  ~a     = 0x%02X = %s   NON : inverse TOUS les bits\n",
                static_cast<std::uint8_t>(~a), bin8(static_cast<std::uint8_t>(~a)).c_str());

    // DECALAGES : multiplier / diviser par 2, ou deplacer un champ
    std::printf("  a << 1 = 0x%02X = %s   <<  : decale a gauche (x2)\n",
                static_cast<std::uint8_t>(a << 1), bin8(static_cast<std::uint8_t>(a << 1)).c_str());
    std::printf("  a >> 1 = 0x%02X = %s   >>  : decale a droite (/2)\n",
                static_cast<std::uint8_t>(a >> 1), bin8(static_cast<std::uint8_t>(a >> 1)).c_str());

    std::puts("\n  \033[1;33mPIEGE C++\033[0m : a << 1 promeut d'abord a en int (32 bits).");
    std::puts("  Sans le cast en uint8_t, 0x80 << 1 donne 0x100, pas 0x00 !");
}

// ===========================================================================
//  3. LES 4 IDIOMES QUE TU ECRIRAS LE PLUS SOUVENT
// ===========================================================================
static void section_idiomes()
{
    titre("3. LES 4 IDIOMES DU PROJET");

    std::uint8_t reg = 0x00;

    // (a) TESTER un bit  -> "l'ecran est-il allume ?"  (bit 7 de LCDC)
    std::printf("  TESTER  : (v >> n) & 1\n");
    reg = 0x80;
    std::printf("            reg=0x%02X, bit 7 = %u  -> LCD allume\n", reg, (reg >> 7) & 1);

    // (b) POSER un bit -> "lever le drapeau d'interruption VBlank" (bit 0 de IF)
    std::printf("\n  POSER   : v |= (1 << n)\n");
    reg = 0x00;
    std::printf("            avant  reg=0x%02X = %s\n", reg, bin8(reg).c_str());
    reg |= (1 << 0);
    std::printf("            apres  reg=0x%02X = %s  -> IRQ VBlank levee\n", reg, bin8(reg).c_str());

    // (c) EFFACER un bit -> "acquitter l'interruption"
    std::printf("\n  EFFACER : v &= ~(1 << n)\n");
    reg = 0xFF;
    std::printf("            avant  reg=0x%02X = %s\n", reg, bin8(reg).c_str());
    reg &= static_cast<std::uint8_t>(~(1 << 3));
    std::printf("            apres  reg=0x%02X = %s  -> bit 3 efface\n", reg, bin8(reg).c_str());

    // (d) EXTRAIRE un champ -> decoder un opcode
    std::printf("\n  EXTRAIRE: (v >> debut) & masque\n");
    const std::uint8_t opcode = 0x42;   // 0100 0010 = LD B, D
    const std::uint8_t dst = (opcode >> 3) & 0x07;   // bits 3-5
    const std::uint8_t src = (opcode >> 0) & 0x07;   // bits 0-2
    std::printf("            opcode 0x%02X = %s\n", opcode, bin8(opcode).c_str());
    std::printf("            destination = (op >> 3) & 0x07 = %u   (0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A)\n", dst);
    std::printf("            source      = (op >> 0) & 0x07 = %u\n", src);
    std::printf("            => \033[1;32mLD B, D\033[0m  -- decode SANS aucun switch/case !\n");
}

// ===========================================================================
//  4. PAIRES DE REGISTRES : composer et decomposer du 16 bits
// ===========================================================================
static void section_paires()
{
    titre("4. PAIRES DE REGISTRES (H et L <-> HL)");

    std::uint8_t h = 0x12;
    std::uint8_t l = 0x34;

    // Composer : l'octet HAUT monte de 8 bits, l'octet BAS reste en place
    std::uint16_t hl = static_cast<std::uint16_t>((h << 8) | l);
    std::printf("  h = 0x%02X = %s\n", h, bin8(h).c_str());
    std::printf("  l = 0x%02X = %s\n", l, bin8(l).c_str());
    std::printf("  hl = (h << 8) | l = 0x%04X = %s\n\n", hl, bin16(hl).c_str());

    // Decomposer : l'inverse
    hl = 0xABCD;
    h = static_cast<std::uint8_t>(hl >> 8);      // octet haut
    l = static_cast<std::uint8_t>(hl & 0x00FF);  // octet bas
    std::printf("  hl = 0x%04X  ->  h = hl >> 8 = 0x%02X   l = hl & 0xFF = 0x%02X\n", 0xABCD, h, l);

    std::puts("\n  \033[1;33mA SAVOIR\033[0m : ce CPU est LITTLE-ENDIAN. En memoire, l'adresse");
    std::puts("  0xABCD est stockee CD puis AB (octet bas en premier).");
}

// ===========================================================================
//  5. LE WRAP-AROUND : la source de bugs n.1 des debutants
// ===========================================================================
static void section_wraparound()
{
    titre("5. WRAP-AROUND (debordement silencieux)");

    std::uint8_t x = 255;
    std::printf("  uint8_t x = 255;  x++;   ->  x = %u    (et pas 256 !)\n", ++x);

    std::uint8_t y = 0;
    std::printf("  uint8_t y = 0;    y--;   ->  y = %u  (et pas -1 !)\n", --y);

    std::uint16_t pc = 0xFFFF;
    std::printf("  uint16_t pc = 0xFFFF; pc++; ->  pc = 0x%04X\n", ++pc);

    std::puts("\n  C'est le comportement du VRAI materiel : les registres sont des");
    std::puts("  fils electriques en nombre fixe, ils ne peuvent pas 'deborder'.");
    std::puts("  \033[1;31mDANGER\033[0m : si tu ecris 'int a = 255; a++;' tu obtiens 256 et ton");
    std::puts("  emulateur devient faux. Utilise TOUJOURS uint8_t / uint16_t.");
}

// ===========================================================================
//  6. LE VRAI EXERCICE DU PROJET : ADD A, B et ses 4 flags
// ===========================================================================
static void add_a_b(std::uint8_t a, std::uint8_t b)
{
    // On calcule sur 16 bits pour VOIR la retenue sortante.
    const std::uint16_t plein = static_cast<std::uint16_t>(a) + static_cast<std::uint16_t>(b);
    const std::uint8_t  res   = static_cast<std::uint8_t>(plein);

    const bool Z = (res == 0);                              // resultat nul
    const bool N = false;                                   // ce n'est pas une soustraction
    const bool H = ((a & 0x0F) + (b & 0x0F)) > 0x0F;        // retenue du bit 3 vers le bit 4
    const bool C = (plein > 0xFF);                           // retenue hors du bit 7

    std::printf("  ADD A,B  A=0x%02X B=0x%02X  ->  A=0x%02X   Z=%d N=%d H=%d C=%d\n",
                a, b, res, Z, N, H, C);
}

static void section_flags()
{
    titre("6. ADD A, B : LES 4 FLAGS (le coeur du CPU)");

    std::puts("  Z = resultat nul | N = c'etait une soustraction");
    std::puts("  H = retenue du quartet bas vers le quartet haut (half-carry)");
    std::puts("  C = retenue hors de l'octet (carry)\n");

    add_a_b(0x0F, 0x01);   // H seul : 0x0F + 0x01 deborde du quartet bas
    add_a_b(0xFF, 0x01);   // tout : resultat 0x00 -> Z, H et C
    add_a_b(0x00, 0x00);   // Z seul
    add_a_b(0x08, 0x08);   // H seul (8+8 = 16 = deborde le quartet)
    add_a_b(0xF0, 0x10);   // C seul, sans H
    add_a_b(0x3A, 0x05);   // aucun flag

    std::puts("\n  \033[1;33mLE HALF-CARRY EST LE BUG N.1 DU PROJET.\033[0m");
    std::puts("  La formule a graver :   H = ((a & 0x0F) + (b & 0x0F)) > 0x0F");
    std::puts("  Traduction : on additionne SEULEMENT les quartets bas et on");
    std::puts("  regarde si ca deborde. Le flag H sert a l'instruction DAA.");
}

// ===========================================================================
//  7. EXERCICES : reponds AVANT de lire la reponse
// ===========================================================================
static void section_exercices()
{
    titre("7. EXERCICES (cache la colonne de droite)");

    struct Exo { const char *question; unsigned reponse; const char *explication; };

    const Exo exos[] = {
        {"0x0F & 0xF0",                       0x0F & 0xF0, "aucun bit commun"},
        {"0x3C | 0xC3",                       0x3C | 0xC3, "0011 1100 | 1100 0011"},
        {"0xFF ^ 0x0F",                       0xFF ^ 0x0F, "XOR avec 0x0F inverse le quartet bas"},
        {"(0xB7 >> 4) & 0x0F",               (0xB7 >> 4) & 0x0F, "extraire le quartet HAUT de 0xB7"},
        {"0xB7 & 0x0F",                       0xB7 & 0x0F, "extraire le quartet BAS de 0xB7"},
        {"(0x47 >> 3) & 0x07",               (0x47 >> 3) & 0x07, "champ destination de l'opcode 0x47"},
        {"1 << 6",                            1u << 6,     "poser le bit 6"},
        {"(uint8_t)~(1 << 2)",     (unsigned)(std::uint8_t)~(1 << 2), "masque pour EFFACER le bit 2"},
        {"(0x12 << 8) | 0x34",               (0x12 << 8) | 0x34, "composer HL depuis H=0x12 L=0x34"},
        {"(uint8_t)(0x80 << 1)",   (unsigned)(std::uint8_t)(0x80 << 1), "le bit 7 sort : il est perdu"},
    };

    std::printf("  %-28s | %s\n", "expression", "resultat");
    std::printf("  %-28s-+-%s\n", "----------------------------", "--------");
    for (const Exo &e : exos)
        std::printf("  %-28s | 0x%02X  (%s)\n", e.question, e.reponse, e.explication);

    std::puts("\n  \033[1;32mVALIDATION\033[0m : si tu retrouves les 10 resultats de tete,");
    std::puts("  tu es pret pour l'etape 3 (le Bus).");
}

int main()
{
    std::puts("\n\033[1;35m##########################################################\033[0m");
    std::puts("\033[1;35m#   RetroEmu - Etape 0 : hexadecimal et operations bit    #\033[0m");
    std::puts("\033[1;35m##########################################################\033[0m");

    section_hexa();
    section_operations();
    section_idiomes();
    section_paires();
    section_wraparound();
    section_flags();
    section_exercices();

    std::puts("");
    return 0;
}
