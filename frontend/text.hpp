#pragma once
#include "runtime.hpp"
#include <filesystem>
#include <memory>

struct TextPixels { int width, height; std::vector<unsigned char> rgba; };
class TextRasterizer {
public:
    TextRasterizer(const std::filesystem::path& root, const wolf::Json& game, const wolf::Json& colors);
    ~TextRasterizer();
    TextPixels render(const std::string& text, const wolf::Json& style);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
