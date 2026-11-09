#pragma once
#include "runtime.hpp"
#include <filesystem>
#include <memory>

struct TextPixels {
    int width, height;
    std::vector<unsigned char> rgba;
    int logical_width = 0, logical_height = 0, border = 0;
};
// Seconds at which each visible codepoint appears; formatting consumes no time.
std::vector<double> text_reveal_times(const std::string& text, int speed);
class TextRasterizer {
public:
    TextRasterizer(const std::filesystem::path& root, const wolf::Json& game, const wolf::Json& colors);
    ~TextRasterizer();
    TextPixels render(const std::string& text, const wolf::Json& style);
    std::pair<int, int> measure(const std::string& text, const wolf::Json& style);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
