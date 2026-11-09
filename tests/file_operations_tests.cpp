#include "file_operations.hpp"
#include <stb/stb_image.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>

int main() {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* target = nullptr;
    try {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("Screenshot test", 0, 0, 64, 56, SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error(SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 64, 56);
        if (!target || SDL_SetRenderTarget(renderer, target) != 0) throw std::runtime_error(SDL_GetError());
        SDL_SetRenderDrawColor(renderer, 40, 110, 210, 255); SDL_RenderClear(renderer);
        SDL_Rect marker{4, 4, 12, 12};
        SDL_SetRenderDrawColor(renderer, 230, 30, 70, 255); SDL_RenderFillRect(renderer, &marker);
        const auto temporary = std::filesystem::temp_directory_path();
        const auto base = temporary / ("wolf-screenshots-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        wolf::Resources writable(base.u8string());
        const wolf::Json operations = wolf::Json::array({{{"operation", 0}, {"path", "Save/preview.jpg"}},
            {{"operation", 4}, {"path", "Save/preview.png"}}});
        // The extra 16 rows represent touch controls and must not enter a game thumbnail.
        process_file_operations(renderer, writable, operations, 64, 40);
        for (const char* name : {"Save/preview.jpg", "Save/preview.png"}) {
            std::ifstream stream(writable.path(name), std::ios::binary);
            const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            int width = 0, height = 0, channels = 0;
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4), stbi_image_free);
            if (!pixels || width != 64 || height != 40) throw std::runtime_error("Screenshot did not decode at game dimensions");
            const auto sample = (30 * width + 30) * 4;
            if (std::abs(pixels.get()[sample] - 40) > 4 || std::abs(pixels.get()[sample + 2] - 210) > 4)
                throw std::runtime_error("Screenshot colors do not match rendered pixels");
        }
        process_file_operations(renderer, writable, wolf::Json::array({{{"operation", 3}, {"path", "Save/preview.jpg"}}}), 64, 40);
        if (std::filesystem::exists(writable.path("Save/preview.jpg")) || !std::filesystem::exists(writable.path("Save/preview.png")))
            throw std::runtime_error("Thumbnail deletion affected the wrong file");
        if (!std::filesystem::equivalent(base.parent_path(), temporary) || base.filename().u8string().compare(0, 17, "wolf-screenshots-") != 0)
            throw std::runtime_error("Invalid screenshot test cleanup path");
        std::filesystem::remove_all(base);
        SDL_DestroyTexture(target); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        std::cout << "Native JPEG/PNG thumbnail readback, decoding and deletion passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (target) SDL_DestroyTexture(target);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); std::cerr << error.what() << '\n'; return 1;
    }
}
