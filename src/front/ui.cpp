#include "retroemu/front/ui.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "retroemu/front/font.hpp"

namespace retroemu {
namespace {

SDL_Color to_sdl(UiColor c) { return SDL_Color{c.r, c.g, c.b, c.a}; }

bool ends_with_rom_extension(const std::string &name)
{
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;

    std::string extension = name.substr(dot);
    for (char &c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".gb" || extension == ".gbc";
}

}  // namespace

// ---------------------------------------------------------------------------
//  Font texture
// ---------------------------------------------------------------------------
bool Ui::init(SDL_Renderer *renderer, int scale)
{
    renderer_ = renderer;
    scale_    = scale < 1 ? 1 : scale;

    // One strip, 95 glyphs wide. Set pixels are opaque white so the texture
    // can be tinted per draw with SDL_SetTextureColorMod.
    constexpr int kGlyphs = kFontLastChar - kFontFirstChar + 1;
    const int     width   = kGlyphs * kFontWidth;

    std::vector<u32> pixels(static_cast<std::size_t>(width) * kFontHeight, 0x00000000u);
    for (int glyph = 0; glyph < kGlyphs; ++glyph) {
        for (int row = 0; row < kFontHeight; ++row) {
            const u8 bits = kFont8x8[glyph][row];
            for (int column = 0; column < kFontWidth; ++column) {
                // The least significant bit is the leftmost pixel in this
                // font, the opposite of the console's own tile format.
                if ((bits >> column) & 1)
                    pixels[static_cast<std::size_t>(row) * width + glyph * kFontWidth + column] =
                        0xFFFFFFFFu;
            }
        }
    }

    font_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STATIC, width, kFontHeight);
    if (font_ == nullptr) return false;

    SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(font_, nullptr, pixels.data(), width * static_cast<int>(sizeof(u32)));
    return true;
}

void Ui::shutdown()
{
    if (font_ != nullptr) { SDL_DestroyTexture(font_); font_ = nullptr; }
}

void Ui::text(int x, int y, const std::string &value, UiColor colour)
{
    if (font_ == nullptr) return;

    SDL_SetTextureColorMod(font_, colour.r, colour.g, colour.b);
    SDL_SetTextureAlphaMod(font_, colour.a);

    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < kFontFirstChar || c > kFontLastChar) continue;

        const SDL_Rect source{(c - kFontFirstChar) * kFontWidth, 0, kFontWidth, kFontHeight};
        const SDL_Rect destination{x + static_cast<int>(i) * glyph_w(), y, glyph_w(), glyph_h()};
        SDL_RenderCopy(renderer_, font_, &source, &destination);
    }
}

void Ui::fill(int x, int y, int w, int h, UiColor colour)
{
    const SDL_Color c = to_sdl(colour);
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    const SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(renderer_, &rect);
}

void Ui::outline(int x, int y, int w, int h, UiColor colour)
{
    const SDL_Color c = to_sdl(colour);
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    const SDL_Rect rect{x, y, w, h};
    SDL_RenderDrawRect(renderer_, &rect);
}

void draw_button(Ui &ui, const UiButton &button, bool active)
{
    ui.fill(button.rect.x, button.rect.y, button.rect.w, button.rect.h,
            active ? kUiHighlight : kUiButton);
    ui.outline(button.rect.x, button.rect.y, button.rect.w, button.rect.h, kUiAccent);

    const int text_x = button.rect.x + (button.rect.w - ui.text_width(button.label)) / 2;
    const int text_y = button.rect.y + (button.rect.h - ui.glyph_h()) / 2;
    ui.text(text_x, text_y, button.label, button.enabled ? kUiText : kUiTextDim);
}

// ---------------------------------------------------------------------------
//  File browsing
// ---------------------------------------------------------------------------
std::vector<FileEntry> list_directory(const std::string &path)
{
    std::vector<FileEntry> directories;
    std::vector<FileEntry> cartridges;

    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) return directories;

    while (const dirent *entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == ".") continue;
        if (name != ".." && !name.empty() && name[0] == '.') continue;   // hidden

        struct stat info {};
        if (stat(join_path(path, name).c_str(), &info) != 0) continue;

        if (S_ISDIR(info.st_mode))              directories.push_back({name, true});
        else if (ends_with_rom_extension(name)) cartridges.push_back({name, false});
    }
    closedir(dir);

    auto by_name = [](const FileEntry &a, const FileEntry &b) { return a.name < b.name; };
    std::sort(directories.begin(), directories.end(), by_name);
    std::sort(cartridges.begin(), cartridges.end(), by_name);

    directories.insert(directories.end(), cartridges.begin(), cartridges.end());
    return directories;
}

std::string join_path(const std::string &directory, const std::string &entry)
{
    if (entry == "..") return parent_directory(directory);
    if (directory.empty() || directory == "/") return "/" + entry;
    return directory + "/" + entry;
}

std::string parent_directory(const std::string &path)
{
    if (path.empty() || path == "/") return "/";

    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') trimmed.pop_back();

    const auto slash = trimmed.rfind('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return trimmed.substr(0, slash);
}

}  // namespace retroemu
