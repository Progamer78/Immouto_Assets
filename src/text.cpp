#include "text.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
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
struct Glyph { Font* font; int cp, size, edge, line, shadow; float x, y, scale; bool antialias; Color color, edge_color; };
struct Line { float width = 0; int size = 0, align = 0; bool occupied = false; float top = 0; };
}
struct TextRasterizer::Impl {
    wolf::Resources resources;
    wolf::Json game, colors;
    std::map<int, std::unique_ptr<Font>> fonts;
    std::map<std::string, std::pair<int, int>> measurements;
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
std::vector<double> text_reveal_times(const std::string& text, int speed) {
    std::vector<double> result;
    double time = 0;
    bool instant = false;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 3, "<L>") == 0 || text.compare(i, 3, "<C>") == 0 || text.compare(i, 3, "<R>") == 0) { i += 3; continue; }
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            if (text[i] == 'E' || text[i] == 'N') { ++i; continue; }
            if (text[i] == '>' || text[i] == '<') { instant = text[i++] == '>'; continue; }
            if (text.compare(i, 2, "A+") == 0 || text.compare(i, 2, "A-") == 0) { i += 2; continue; }
            if (text[i] != '\\') {
                const auto open = text.find('[', i), close = open == std::string::npos ? std::string::npos : text.find(']', open);
                if (open == std::string::npos || close == std::string::npos || open - i > 6) throw std::runtime_error("Unsupported text-picture reveal control");
                const auto tag = text.substr(i, open - i);
                if (tag == "sp") speed = std::stoi(text.substr(open + 1, close - open - 1));
                else if (tag != "f" && tag != "f+" && tag != "font" && tag != "c" && tag != "cE" && tag != "wE" && tag != "space" && tag != "-" &&
                         tag != "ax" && tag != "ay" && tag != "mx" && tag != "my" && tag != "bx")
                    throw std::runtime_error("Unsupported text-picture reveal control: " + tag);
                i = close + 1; continue;
            }
        }
        const int cp = codepoint(text, i);
        if (cp == '\r' || cp == '\n') continue;
        if (!instant && speed > 0) time += 1.0 / speed;
        result.push_back(time);
    }
    return result;
}
std::pair<int, int> TextRasterizer::measure(const std::string& text, const wolf::Json& style) {
    const auto key = style.dump() + text;
    const auto found = impl_->measurements.find(key);
    if (found != impl_->measurements.end()) return found->second;
    auto invisible = style; invisible["visible_chars"] = 0;
    const auto pixels = render(text, invisible);
    const auto result = std::make_pair(pixels.logical_width, pixels.logical_height);
    impl_->measurements.emplace(key, result); return result;
}
TextPixels TextRasterizer::render(const std::string& text, const wolf::Json& style) {
    int size = style.at("size"), font_id = 0, edge = 0, align = 0, spacing = 0;
    if (size < 1 || size > 512 || style.at("speed") != 0) throw std::runtime_error("Unsupported text picture size or reveal speed");
    bool antialias = style.at("antialias") == 0;
    const int shadow = style.at("shadow");
    Color color = impl_->color(0), edge_color{0, 0, 0};
    const auto& flags = impl_->game.at("flags");
    int line_spacing = flags.at(11), padding = flags.at(10);
    float cursor_x = 0, cursor_y = 0, next_line_x = 0;
    std::vector<Glyph> glyphs;
    std::vector<Line> lines(1); lines.back().size = size;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\r') { ++i; continue; }
        if (text[i] == '\n') {
            ++i; cursor_y += lines.back().size + (lines.back().occupied ? line_spacing : 0); cursor_x = next_line_x;
            lines.push_back({cursor_x, size, align, false, cursor_y}); continue;
        }
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
            else if (tag == "ax") { cursor_x = static_cast<float>(value); lines.back().width = std::max(lines.back().width, cursor_x); }
            else if (tag == "ay") { cursor_y = static_cast<float>(value); if (!lines.back().occupied) lines.back().top = cursor_y; }
            else if (tag == "mx") cursor_x += value;
            else if (tag == "my") { cursor_y += value; if (!lines.back().occupied) lines.back().top = cursor_y; }
            else if (tag == "bx") next_line_x = static_cast<float>(value);
            else if (tag == "sp") {} // Reveal timing is evaluated by the interpreter.
            else throw std::runtime_error("Unsupported text control: " + tag);
            if (size < 1 || size > 512) throw std::runtime_error("Unsupported font size");
            lines.back().size = std::max(lines.back().size, size); continue;
        }
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '\\') ++i;
        const int cp = codepoint(text, i);
        auto& font = impl_->font(font_id);
        // The supplied engine draws TAB with the font's default glyph (20px
        // wide at size 20), rather than expanding it into tab-stop spaces.
        if (cp != '\t' && !stbtt_FindGlyphIndex(&font.info, cp)) throw std::runtime_error("Missing game font glyph " + std::to_string(cp));
        const float scale = stbtt_ScaleForPixelHeight(&font.info, static_cast<float>(size));
        auto& line = lines.back(); line.size = std::max(line.size, size); line.occupied = true;
        glyphs.push_back({&font, cp, size, edge, static_cast<int>(lines.size() - 1), shadow, cursor_x, cursor_y, scale, antialias, color, edge_color});
        int advance = 0, bearing = 0; stbtt_GetCodepointHMetrics(&font.info, cp, &advance, &bearing);
        cursor_x += std::floor(advance * scale) + padding - spacing; line.width = std::max(line.width, cursor_x);
    }
    const int border = 6 + std::max(0, shadow);
    int logical_width = 0, logical_height = 0;
    for (const auto& line : lines) {
        logical_width = std::max(logical_width, static_cast<int>(std::ceil(line.width)));
        logical_height = std::max(logical_height, static_cast<int>(std::ceil(line.top)) + line.size);
    }
    for (const auto& glyph : glyphs) logical_height = std::max(logical_height, static_cast<int>(std::ceil(glyph.y)) + glyph.size);
    const int width = std::max(1, logical_width) + border * 2, height = std::max(1, logical_height) + border * 2;
    if (width > 16384 || height > 16384 || static_cast<std::uint64_t>(width) * height > 16 * 1024 * 1024)
        throw std::runtime_error("Text picture is too large");
    TextPixels output{width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4)};
    output.logical_width = logical_width; output.logical_height = logical_height; output.border = border;
    auto paint = [&](int x, int y, unsigned char alpha, Color ink) {
        if (x < 0 || y < 0 || x >= width || y >= height || alpha == 0) return;
        auto* pixel = &output.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
        const int combined = alpha + pixel[3] * (255 - alpha) / 255;
        for (int c = 0; c < 3; ++c) pixel[c] = static_cast<unsigned char>((ink[c] * alpha + pixel[c] * pixel[3] * (255 - alpha) / 255) / combined);
        pixel[3] = static_cast<unsigned char>(combined);
    };
    const auto visible = static_cast<std::size_t>(std::max(0, style.value("visible_chars", std::numeric_limits<int>::max())));
    for (std::size_t glyph_index = 0; glyph_index < glyphs.size() && glyph_index < visible; ++glyph_index) {
        const auto& glyph = glyphs.at(glyph_index);
        int ascent = 0, descent = 0, gap = 0; stbtt_GetFontVMetrics(&glyph.font->info, &ascent, &descent, &gap);
        int w = 0, h = 0, xoff = 0, yoff = 0;
        auto* bitmap = stbtt_GetCodepointBitmap(&glyph.font->info, glyph.scale, glyph.scale, glyph.cp, &w, &h, &xoff, &yoff);
        const auto& line = lines.at(glyph.line);
        const float offset = line.align == 1 ? (width - border * 2 - line.width) / 2 : line.align == 2 ? width - border * 2 - line.width : 0;
        const int x = border + static_cast<int>(std::round(glyph.x + offset)) + xoff;
        const int y = border + static_cast<int>(std::round(glyph.y + ascent * glyph.scale)) + yoff;
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
