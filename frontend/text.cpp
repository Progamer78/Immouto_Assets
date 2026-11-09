#include "text.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

namespace {
int codepoint(const std::string& text, std::size_t& position) {
    const auto first = static_cast<unsigned char>(text.at(position++));
    if (first < 128) return first;
    const int count = first < 224 ? 1 : first < 240 ? 2 : 3;
    int value = first & ((1 << (6 - count)) - 1);
    for (int i = 0; i < count; ++i) value = (value << 6) | (static_cast<unsigned char>(text.at(position++)) & 63);
    return value;
}
using Color = std::array<unsigned char, 3>;
struct Font {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    explicit Font(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        if (bytes.empty() || !stbtt_InitFont(&info, bytes.data(), stbtt_GetFontOffsetForIndex(bytes.data(), 0)))
            throw std::runtime_error("Cannot load game font: " + path.u8string());
    }
};
struct Glyph { Font* font; int cp, size, edge, line, shadow; float x, scale; bool antialias; Color color, edge_color; };
struct Line { float width = 0; int size = 0, align = 0; bool occupied = false; };
}
struct TextRasterizer::Impl {
    wolf::Resources resources;
    wolf::Json game, colors;
    std::map<int, std::unique_ptr<Font>> fonts;
    Impl(const std::filesystem::path& root, const wolf::Json& config, const wolf::Json& palette) :
        resources(root.u8string()), game(config), colors(palette) {}
    Font& font(int id) {
        if (id < 0 || id >= static_cast<int>(game.at("fonts").size())) throw std::runtime_error("Invalid game font index");
        auto& result = fonts[id];
        if (!result) {
            const auto name = game.at("fonts").at(id).get<std::string>();
            const auto file = resources.path("Data/" + name + ".ttf");
            if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("Configured game font is not resolved: " + name);
            result = std::make_unique<Font>(file);
        }
        return *result;
    }
    Color color(int id) {
        const auto& row = colors.at(id);
        return {static_cast<unsigned char>(row.at(0).get<int>()), static_cast<unsigned char>(row.at(1).get<int>()), static_cast<unsigned char>(row.at(2).get<int>())};
    }
};
TextRasterizer::TextRasterizer(const std::filesystem::path& root, const wolf::Json& game, const wolf::Json& colors) : impl_(std::make_unique<Impl>(root, game, colors)) {}
TextRasterizer::~TextRasterizer() = default;
TextPixels TextRasterizer::render(const std::string& text, const wolf::Json& style) {
    int size = style.at("size"), font_id = 0, edge = 0, align = 0, spacing = 0;
    if (size < 1 || size > 512 || style.at("speed") != 0) throw std::runtime_error("Unsupported text picture size or reveal speed");
    bool antialias = style.at("antialias") == 0;
    const int shadow = style.at("shadow");
    Color color = impl_->color(0), edge_color{0, 0, 0};
    const auto& flags = impl_->game.at("flags");
    int line_spacing = flags.at(11), padding = flags.at(10);
    std::vector<Glyph> glyphs;
    std::vector<Line> lines(1); lines.back().size = size;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\r') { ++i; continue; }
        if (text[i] == '\n') { ++i; lines.push_back({0, size, align, false}); continue; }
        if (text.compare(i, 3, "<L>") == 0 || text.compare(i, 3, "<C>") == 0 || text.compare(i, 3, "<R>") == 0) {
            align = text[i + 1] == 'C' ? 1 : text[i + 1] == 'R' ? 2 : 0;
            lines.back().align = align; i += 3; continue;
        }
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] != '\\') {
            const auto begin = ++i;
            if (text[i] == 'E' || text[i] == 'N') { edge = text[i++] == 'E' ? 1 : 0; continue; }
            if (text.compare(i, 2, "A+") == 0 || text.compare(i, 2, "A-") == 0) { antialias = text[i + 1] == '+'; i += 2; continue; }
            if (text[i] == '>' || text[i] == '<') { ++i; continue; }
            const auto open = text.find('[', i), close = open == std::string::npos ? std::string::npos : text.find(']', open);
            if (open == std::string::npos || close == std::string::npos || open - begin > 6)
                throw std::runtime_error("Unsupported text control at byte " + std::to_string(begin - 1));
            const auto tag = text.substr(begin, open - begin);
            const auto value = std::stoi(text.substr(open + 1, close - open - 1));
            i = close + 1;
            if (tag == "f") size = value;
            else if (tag == "f+") size += value;
            else if (tag == "font") font_id = value;
            else if (tag == "c") color = impl_->color(value);
            else if (tag == "cE") edge_color = impl_->color(value);
            else if (tag == "wE") edge = std::clamp(value, 1, 5);
            else if (tag == "space") line_spacing = value;
            else if (tag == "-") spacing = value;
            else throw std::runtime_error("Unsupported text control: " + tag);
            if (size < 1 || size > 512) throw std::runtime_error("Unsupported font size");
            lines.back().size = std::max(lines.back().size, size); continue;
        }
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '\\') ++i;
        const int cp = codepoint(text, i);
        auto& font = impl_->font(font_id);
        if (!stbtt_FindGlyphIndex(&font.info, cp)) throw std::runtime_error("Missing game font glyph " + std::to_string(cp));
        const float scale = stbtt_ScaleForPixelHeight(&font.info, static_cast<float>(size));
        auto& line = lines.back(); line.size = std::max(line.size, size); line.occupied = true;
        glyphs.push_back({&font, cp, size, edge, static_cast<int>(lines.size() - 1), shadow, line.width, scale, antialias, color, edge_color});
        int advance = 0, bearing = 0; stbtt_GetCodepointHMetrics(&font.info, cp, &advance, &bearing);
        line.width += advance * scale + padding - spacing;
    }
    const int border = 6 + std::max(0, shadow);
    int width = 1, height = border * 2;
    std::vector<int> tops;
    for (const auto& line : lines) {
        width = std::max(width, static_cast<int>(std::ceil(line.width)) + border * 2);
        tops.push_back(height - border); height += line.size + (line.occupied ? line_spacing : 0);
    }
    if (width > 16384 || height > 16384 || static_cast<std::uint64_t>(width) * height > 16 * 1024 * 1024)
        throw std::runtime_error("Text picture is too large");
    TextPixels output{width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4)};
    auto paint = [&](int x, int y, unsigned char alpha, Color ink) {
        if (x < 0 || y < 0 || x >= width || y >= height || alpha == 0) return;
        auto* pixel = &output.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
        const int combined = alpha + pixel[3] * (255 - alpha) / 255;
        for (int c = 0; c < 3; ++c) pixel[c] = static_cast<unsigned char>((ink[c] * alpha + pixel[c] * pixel[3] * (255 - alpha) / 255) / combined);
        pixel[3] = static_cast<unsigned char>(combined);
    };
    for (const auto& glyph : glyphs) {
        int ascent = 0, descent = 0, gap = 0; stbtt_GetFontVMetrics(&glyph.font->info, &ascent, &descent, &gap);
        int w = 0, h = 0, xoff = 0, yoff = 0;
        auto* bitmap = stbtt_GetCodepointBitmap(&glyph.font->info, glyph.scale, glyph.scale, glyph.cp, &w, &h, &xoff, &yoff);
        const auto& line = lines.at(glyph.line);
        const float offset = line.align == 1 ? (width - border * 2 - line.width) / 2 : line.align == 2 ? width - border * 2 - line.width : 0;
        const int x = border + static_cast<int>(std::round(glyph.x + offset)) + xoff;
        const int y = tops.at(glyph.line) + static_cast<int>(std::round(ascent * glyph.scale)) + yoff;
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
            const auto a = glyph.antialias ? bitmap[yy * w + xx] : bitmap[yy * w + xx] >= 128 ? 255 : 0;
            if (glyph.shadow > 0) paint(x + xx + glyph.shadow, y + yy + glyph.shadow, a, {0, 0, 0});
            for (int dy = -glyph.edge; dy <= glyph.edge; ++dy) for (int dx = -glyph.edge; dx <= glyph.edge; ++dx)
                if (dx || dy) paint(x + xx + dx, y + yy + dy, a, glyph.edge_color);
        }
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx)
            paint(x + xx, y + yy, glyph.antialias ? bitmap[yy * w + xx] : bitmap[yy * w + xx] >= 128 ? 255 : 0, glyph.color);
        stbtt_FreeBitmap(bitmap, nullptr);
    }
    return output;
}
