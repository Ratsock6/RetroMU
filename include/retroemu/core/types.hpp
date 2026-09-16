#pragma once
// ===========================================================================
//  Types de base de l'emulateur.
// ===========================================================================
//  Le materiel emule est 8/16 bits. Utiliser 'int' partout ferait disparaitre
//  le wrap-around (255 + 1 doit donner 0, pas 256) et rendrait l'emulation
//  fausse. Ces alias rendent la largeur explicite a chaque declaration.
// ===========================================================================

#include <cstddef>
#include <cstdint>

namespace retroemu {

using u8  = std::uint8_t;    // un registre 8 bits, une case memoire
using u16 = std::uint16_t;   // une adresse, une paire de registres
using u32 = std::uint32_t;   // un compteur de cycles, une couleur ARGB
using u64 = std::uint64_t;   // un compteur de cycles cumule

using i8  = std::int8_t;     // un deplacement relatif signe (instruction JR)
using i16 = std::int16_t;
using i32 = std::int32_t;

// --- Geometrie de l'ecran (identique sur DMG et CGB) ----------------------
inline constexpr int kScreenWidth  = 160;
inline constexpr int kScreenHeight = 144;
inline constexpr std::size_t kScreenPixels =
    static_cast<std::size_t>(kScreenWidth) * kScreenHeight;

}  // namespace retroemu
