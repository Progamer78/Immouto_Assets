#include "file_operations.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace {
void append_image(void* context, void* bytes, int count) {
    static_cast<std::string*>(context)->append(static_cast<const char*>(bytes), count);
}
}

void write_import_preview(wolf::Resources& writable,int width,int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*3,40);
    std::string encoded;
    if (!stbi_write_jpg_to_func(append_image,&encoded,width,height,3,pixels.data(),75)) throw std::runtime_error("Cannot encode imported save preview");
    wolf::atomic_write(writable.path("Save/pc-import-preview.jpg").u8string(),encoded);
    writable.invalidate();
}

void process_file_operations(SDL_Renderer* renderer, wolf::Resources& writable,
    const wolf::Json& operations, int width, int height) {
    std::vector<unsigned char> pixels;
    for (const auto& operation : operations) {
        const std::string name = operation.at("path");
        const auto path = writable.path(name);
        const int mode = operation.at("operation");
        if (mode == 3) {
            auto lower = name;
            std::replace(lower.begin(), lower.end(), '\\', '/');
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (lower.compare(0, 5, "data/") == 0 || std::filesystem::is_directory(path))
                throw std::runtime_error("Game file deletion is limited to writable files outside Data");
            std::filesystem::remove(path); // A missing thumbnail is normal.
        } else {
            if (mode != 0 && mode != 4) throw std::runtime_error("Unsupported native file operation");
            if (pixels.empty()) {
                pixels.resize(static_cast<std::size_t>(width) * height * 4);
                const SDL_Rect game{0, 0, width, height};
                if (SDL_RenderReadPixels(renderer, &game, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4) != 0)
                    throw std::runtime_error(SDL_GetError());
            }
            std::string encoded;
            const int written = mode == 0 ? stbi_write_jpg_to_func(append_image, &encoded, width, height, 4, pixels.data(), 85)
                : stbi_write_png_to_func(append_image, &encoded, width, height, 4, pixels.data(), width * 4);
            if (!written) throw std::runtime_error("Cannot encode native screenshot");
            wolf::atomic_write(path.u8string(), encoded);
        }
        writable.invalidate();
    }
}
