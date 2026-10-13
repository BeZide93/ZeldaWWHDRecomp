// The story notebook's text in the game's own menu font (story_notebook_page.h draws Text commands;
// this turns them into pictures of the game's glyphs). Pure code: the glyphs come from the caller
// (game_glyph::read_glyph on the fonts the game loaded, story_notebook.cpp), the result is a host
// image plus picture commands; unit-tested in runtime/tools/story_notebook_test.cpp.
//
// Each distinct line is composed once into a line atlas (an RGBA sheet: white coverage for the
// letters, the icon font's own colours for the button pictures) at the font's own size; a Text
// command becomes one picture of its line, scaled to the command's size and tinted with its colour.
// The atlas is rebuilt only when the page shows a line it doesn't hold yet.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../game_glyph.h"
#include "../button_glyph_art.h"
#include "../button_glyphs.h"
#include "guest_hud.h"

namespace story_notebook::font {

using guestmods::hud::Command;

// the private-use characters of the game's button pictures (the icon glyphs of its message font)
inline constexpr uint32_t kButtonA = 0xE000, kButtonB = 0xE001;
inline const std::string kGlyphA = "\xEE\x80\x80", kGlyphB = "\xEE\x80\x81";  // U+E000, U+E001 in UTF-8

// The next code point of a UTF-8 string (the HUD store only takes valid UTF-8); 0xFFFD on a bad byte
inline uint32_t next_code(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = c & (0x3F >> more);
    if (!more) return 0xFFFD;
    for (; more && i < s.size(); more--) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return more ? 0xFFFD : cp;
}
inline std::vector<uint32_t> codes(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) out.push_back(next_code(s, i));
    return out;
}

// How the button pictures look (input_map::prompt_style, as the game's own prompts are restyled by
// button_glyphs.cpp): the game's icon, the icon in the Xbox colours, or the PlayStation shapes.
struct Prompts {
    enum Face : uint8_t { Original, Xbox, PlayStation } face = Original;
    bool colours = false;
    bool operator==(const Prompts&) const = default;
};

// A font as the page uses it: its glyphs (filled on demand by the caller), its cell height.
struct Face {
    int cell_height = 0;
    std::unordered_map<uint32_t, game_glyph::Glyph> glyphs;
    std::unordered_map<uint32_t, bool> missing;  // asked for, not in the font
    bool ready() const { return cell_height > 0 && !glyphs.empty(); }
    const game_glyph::Glyph* find(uint32_t cp) const {
        auto it = glyphs.find(cp);
        if (it != glyphs.end()) return &it->second;
        if (cp != '?') {  // the font's own stand-in, as the game shows an unknown character
            it = glyphs.find('?');
            if (it != glyphs.end()) return &it->second;
        }
        return nullptr;
    }
    // the characters of `text` the caller still has to ask the game for
    void wanted(const std::string& text, std::vector<uint32_t>& out) const {
        for (uint32_t cp : codes(text))
            if (!glyphs.count(cp) && !missing.count(cp) && std::find(out.begin(), out.end(), cp) == out.end()) out.push_back(cp);
    }
};

// The width of a line in the font's own pixels
inline int line_pixels(const Face& f, const std::string& text) {
    int w = 0;
    for (uint32_t cp : codes(text))
        if (const game_glyph::Glyph* g = f.find(cp)) w += g->advance;
    return w;
}
// The line's width on the page at a text size (the page's sizes are the line height in page units)
inline float measure(const Face& f, const std::string& text, float size) {
    return f.cell_height ? float(line_pixels(f, text)) * size / float(f.cell_height) : 0.0f;
}

// One button picture, 64 x 64 cells scaled to the font's height: the game's icon glyph (in the
// Xbox colours if asked), or the PlayStation shape the port draws for that button.
inline game_glyph::Bitmap button_picture(const game_glyph::Glyph* icon, int face, const Prompts& look, int height) {
    game_glyph::Bitmap out;
    if (look.face == Prompts::PlayStation || !icon) {
        // the port's own shape on a disc (button_glyph_art.h), or a plain disc when the font has no icon
        out.width = out.height = 64;
        out.rgba.resize(64 * 64 * 4);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                const uint32_t c = button_glyphs::face_shape(face, look.colours, true, x, y);
                uint8_t* p = &out.rgba[(size_t(y) * 64 + x) * 4];
                p[0] = c >> 24, p[1] = c >> 16, p[2] = c >> 8, p[3] = c;
            }
        (void)height;
        return out;
    }
    out = icon->bitmap;
    if (look.face == Prompts::Xbox && look.colours)  // multiplied like the game's vertex colours (button_glyphs::tint)
        for (size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
            const uint32_t t = button_glyphs::tint(uint32_t(out.rgba[i]) << 24 | uint32_t(out.rgba[i + 1]) << 16 |
                                                       uint32_t(out.rgba[i + 2]) << 8 | out.rgba[i + 3], face);
            out.rgba[i] = t >> 24, out.rgba[i + 1] = t >> 16, out.rgba[i + 2] = t >> 8;
        }
    return out;
}

// The line atlas: every line the page shows, composed once.
class Atlas {
public:
    static constexpr int kWidth = 2048, kMaxHeight = 1024, kPad = 2;
    struct Cell {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        int width = 0, height = 0;  // pixels
        bool picture = false;       // a coloured picture (a button): tinted by alpha only
    };
    // true if every line of `lines` is in the atlas, composed with this look and these fonts
    bool holds(const std::vector<std::string>& lines, const Prompts& look, uint64_t fonts) const {
        if (look != look_ || fonts != fonts_ || !image_) return false;
        for (const std::string& l : lines)
            if (!cells_.count(l)) return false;
        return true;
    }
    // Compose the lines (the main font, and `fallback` for characters the main font lacks; the
    // icon font for the button pictures). Lines that don't fit any more are left out (drawn with
    // the overlay font).
    void build(const std::vector<std::string>& lines, const Face& main, const Face* fallback, const Face* icons, const Prompts& look,
               uint64_t fonts) {
        cells_.clear();
        look_ = look, fonts_ = fonts;
        const int h = main.cell_height;
        auto image = std::make_shared<guestmods::hud::Image>();
        image->width = kWidth;
        int x = kPad, y = kPad, used = 0;
        std::vector<std::string> order = lines;  // unique lines
        std::sort(order.begin(), order.end());
        order.erase(std::unique(order.begin(), order.end()), order.end());
        if (h <= 0) order.clear();
        // layout: rows of the cell height, left to right
        struct Place {
            const std::string* text;
            int x, y, w;
        };
        std::vector<Place> places;
        for (const std::string& text : order) {
            const int w = std::max(1, line_width(text, main, fallback, h));
            if (w + 2 * kPad > kWidth) continue;
            if (x + w + kPad > kWidth) x = kPad, y += h + kPad;
            if (y + h + kPad > kMaxHeight) break;
            places.push_back({&text, x, y, w});
            x += w + kPad;
            used = y + h + kPad;
        }
        image->height = std::max(4, used);
        image->rgba.assign(size_t(image->width) * image->height * 4, 0);
        for (const Place& p : places) {
            Cell c;
            c.width = p.w, c.height = h;
            c.u0 = float(p.x) / kWidth, c.v0 = float(p.y) / image->height;
            c.u1 = float(p.x + p.w) / kWidth, c.v1 = float(p.y + h) / image->height;
            c.picture = compose(*image, *p.text, main, fallback, icons, look, p.x, p.y, h);
            cells_[*p.text] = c;
        }
        image_ = image;
        generation_++;
    }
    const Cell* cell(const std::string& line) const {
        auto it = cells_.find(line);
        return it == cells_.end() ? nullptr : &it->second;
    }
    std::shared_ptr<const guestmods::hud::Image> image() const { return image_; }
    uint64_t generation() const { return generation_; }
    void clear() {
        cells_.clear();
        image_.reset();
        generation_++;
    }

    // the button characters' width in a line: as wide as tall (the game's icons are square cells)
    static bool button(uint32_t cp) { return cp == kButtonA || cp == kButtonB; }
    static int line_width(const std::string& text, const Face& main, const Face* fallback, int h) {
        int w = 0;
        for (uint32_t cp : codes(text)) {
            if (button(cp)) {
                w += h;
                continue;
            }
            const game_glyph::Glyph* g = glyph(cp, main, fallback);
            if (g) w += std::max(g->advance, g->left + g->bitmap.width);
        }
        return w;
    }

private:
    static const game_glyph::Glyph* glyph(uint32_t cp, const Face& main, const Face* fallback) {
        auto it = main.glyphs.find(cp);
        if (it != main.glyphs.end()) return &it->second;
        if (fallback) {
            auto f = fallback->glyphs.find(cp);
            if (f != fallback->glyphs.end()) return &f->second;
        }
        return main.find(cp);
    }
    // draw a bitmap into the atlas at (x, y), scaled to height h (bilinear; a glyph of another
    // font's height is fitted to the main font's line)
    static void blit(guestmods::hud::Image& img, const game_glyph::Bitmap& b, int x, int y, int w, int h) {
        if (b.width <= 0 || b.height <= 0 || w <= 0 || h <= 0) return;
        for (int ty = 0; ty < h; ty++)
            for (int tx = 0; tx < w; tx++) {
                const int ax = x + tx, ay = y + ty;
                if (ax < 0 || ay < 0 || ax >= int(img.width) || ay >= int(img.height)) continue;
                const float sx = (tx + 0.5f) * b.width / w - 0.5f, sy = (ty + 0.5f) * b.height / h - 0.5f;
                const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                const float fx = sx - ix, fy = sy - iy;
                float acc[4] = {0, 0, 0, 0};
                for (int k = 0; k < 4; k++) {
                    const int px = std::clamp(ix + (k & 1), 0, b.width - 1), py = std::clamp(iy + (k >> 1), 0, b.height - 1);
                    const float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                    const uint8_t* s = &b.rgba[(size_t(py) * b.width + px) * 4];
                    // premultiplied, so transparent texels don't bleed their colour
                    const float a = s[3] / 255.0f;
                    for (int c = 0; c < 3; c++) acc[c] += s[c] * a * wgt;
                    acc[3] += s[3] * wgt;
                }
                uint8_t* d = &img.rgba[(size_t(ay) * img.width + ax) * 4];
                const float a = acc[3];
                if (a <= 0.0f) continue;
                // over what is already there (glyphs may overlap a little)
                const float da = d[3] / 255.0f, sa = a / 255.0f, oa = sa + da * (1 - sa);
                for (int c = 0; c < 3; c++) {
                    const float sc = acc[c] / a * 255.0f / 255.0f;  // straight colour
                    d[c] = uint8_t(std::clamp((sc * sa + d[c] * da * (1 - sa)) / std::max(oa, 1e-4f), 0.0f, 255.0f));
                }
                d[3] = uint8_t(std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f));
            }
    }
    // the glyph's own pixels at (x, y): no scaling (the main font)
    static void put(guestmods::hud::Image& img, const game_glyph::Bitmap& b, int x, int y) {
        for (int ty = 0; ty < b.height; ty++)
            for (int tx = 0; tx < b.width; tx++) {
                const int ax = x + tx, ay = y + ty;
                if (ax < 0 || ay < 0 || ax >= int(img.width) || ay >= int(img.height)) continue;
                const uint8_t* s = &b.rgba[(size_t(ty) * b.width + tx) * 4];
                uint8_t* d = &img.rgba[(size_t(ay) * img.width + ax) * 4];
                if (s[3] > d[3]) d[0] = s[0], d[1] = s[1], d[2] = s[2], d[3] = s[3];
            }
    }
    // one line; true if it is a coloured picture (only buttons)
    static bool compose(guestmods::hud::Image& img, const std::string& text, const Face& main, const Face* fallback, const Face* icons,
                        const Prompts& look, int x, int y, int h) {
        bool only_buttons = true;
        int pen = x;
        for (uint32_t cp : codes(text)) {
            if (button(cp)) {
                const game_glyph::Glyph* icon = icons ? icons->find(cp) : nullptr;
                if (icon && icon != &icons->glyphs.find(cp)->second) icon = nullptr;  // not the stand-in
                const game_glyph::Bitmap b = button_picture(icon, cp == kButtonA ? 0 : 1, look, h);
                blit(img, b, pen, y, h, h);
                pen += h;
                continue;
            }
            only_buttons = false;
            const game_glyph::Glyph* g = glyph(cp, main, fallback);
            if (!g) continue;
            const bool own = g->bitmap.height == h;
            if (own) put(img, g->bitmap, pen + std::max(0, g->left), y);
            else blit(img, g->bitmap, pen + std::max(0, g->left), y, std::max(1, g->bitmap.width * h / std::max(1, g->bitmap.height)), h);
            pen += std::max(g->advance, g->left + g->bitmap.width);
        }
        return only_buttons;
    }

    std::map<std::string, Cell> cells_;
    std::shared_ptr<guestmods::hud::Image> image_;
    Prompts look_;
    uint64_t fonts_ = 0, generation_ = 0;
};

// The lines a draw list needs from the atlas
inline std::vector<std::string> lines_of(const std::vector<Command>& cmds) {
    std::vector<std::string> out;
    for (const Command& c : cmds)
        if (c.kind == Command::Text && !c.text.empty()) out.push_back(c.text);
    return out;
}

// The page's Text commands as pictures of their atlas lines (kind Picture, u/v set; the caller hands
// them to the HUD store with the atlas image). `font_scale`: the drawn line height per text size.
// Lines not in the atlas stay Text (the overlay font); a button without the atlas becomes a disc
// with its letter.
inline std::vector<Command> to_pictures(const std::vector<Command>& in, const Atlas* atlas, float font_scale = 1.0f) {
    std::vector<Command> out;
    out.reserve(in.size());
    for (const Command& c : in) {
        if (c.kind != Command::Text) {
            out.push_back(c);
            continue;
        }
        const Atlas::Cell* cell = atlas ? atlas->cell(c.text) : nullptr;
        if (!cell) {
            if (c.text == kGlyphA || c.text == kGlyphB) {  // no font: the button as a dark disc with its letter
                const float r = c.size * 0.5f;
                Command disc;
                disc.kind = Command::Circle, disc.x = c.x + r, disc.y = c.y + r, disc.size = r, disc.rgba = 0x3F2A14FF & (0xFFFFFF00u | (c.rgba & 0xFF));
                out.push_back(disc);
                Command letter = c;
                letter.text = c.text == kGlyphA ? "A" : "B";
                letter.x = c.x + r * 0.52f, letter.y = c.y + r * 0.22f, letter.size = c.size * 0.72f;
                letter.rgba = 0xF2E4C200u | (c.rgba & 0xFF);
                out.push_back(letter);
                continue;
            }
            out.push_back(c);
            continue;
        }
        Command p;
        p.kind = Command::Picture;
        const float h = c.size * font_scale, k = h / float(cell->height);
        p.x = c.x, p.y = c.y - (h - c.size) * 0.5f, p.w = float(cell->width) * k, p.h = h;
        p.u0 = cell->u0, p.v0 = cell->v0, p.u1 = cell->u1, p.v1 = cell->v1;
        p.rgba = cell->picture ? (0xFFFFFF00u | (c.rgba & 0xFF)) : c.rgba;
        p.anchor = c.anchor, p.blend = c.blend;
        out.push_back(p);
    }
    return out;
}

}  // namespace story_notebook::font
