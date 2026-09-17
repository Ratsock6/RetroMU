#pragma once
// ===========================================================================
//  On-screen interface widgets.
// ===========================================================================
//  Section IV of the subject (p.6) requires a GUI with at least load, play and
//  pause, and forbids any higher-level framework for the rendering layer. So
//  the interface is drawn with what SDL2 offers: filled rectangles, and a
//  bitmap font blitted from a texture built at startup.
//
//  Everything here is deliberately plain. It has to be unmistakable to a
//  corrector, not pretty.
// ===========================================================================

#include <SDL.h>

#include <string>
#include <vector>

#include "retroemu/core/types.hpp"

namespace retroemu {

struct UiColor { u8 r, g, b, a; };

inline constexpr UiColor kUiBackground = {0x1A, 0x1F, 0x1A, 0xFF};
inline constexpr UiColor kUiPanel      = {0x2A, 0x33, 0x2A, 0xFF};
inline constexpr UiColor kUiButton     = {0x3C, 0x4A, 0x3C, 0xFF};
inline constexpr UiColor kUiHighlight  = {0x5A, 0x7A, 0x3C, 0xFF};
inline constexpr UiColor kUiText       = {0xC8, 0xDC, 0xB4, 0xFF};
inline constexpr UiColor kUiTextDim    = {0x7A, 0x8A, 0x74, 0xFF};
inline constexpr UiColor kUiAccent     = {0x9B, 0xBC, 0x0F, 0xFF};

// Draws text and rectangles. Owns the font texture.
class Ui {
public:
    bool init(SDL_Renderer *renderer, int scale);
    void shutdown();

    int scale() const    { return scale_; }
    int glyph_w() const  { return 8 * scale_; }
    int glyph_h() const  { return 8 * scale_; }
    int text_width(const std::string &text) const
    {
        return static_cast<int>(text.size()) * glyph_w();
    }

    void text(int x, int y, const std::string &value, UiColor colour);
    void fill(int x, int y, int w, int h, UiColor colour);
    void outline(int x, int y, int w, int h, UiColor colour);

private:
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture  *font_     = nullptr;
    int           scale_    = 2;
};

// A labelled clickable region. Named UiButton rather than Button because
// Button already names the console's own eight buttons in joypad.hpp.
struct UiButton {
    SDL_Rect    rect{};
    std::string label;
    bool        enabled = true;

    bool contains(int x, int y) const
    {
        return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
    }
};

void draw_button(Ui &ui, const UiButton &button, bool active);

// --- File browsing ---------------------------------------------------------
struct FileEntry {
    std::string name;
    bool        is_directory = false;
};

// Directories first, then cartridges, both sorted. Anything that is not a
// directory or a .gb / .gbc file is left out: the browser exists to find a
// cartridge, not to explore a disk.
std::vector<FileEntry> list_directory(const std::string &path);

// Join a directory and an entry, resolving "..".
std::string join_path(const std::string &directory, const std::string &entry);

// The directory a path lives in.
std::string parent_directory(const std::string &path);

}  // namespace retroemu
