#include "file_operations.hpp"
#include "window.hpp"
#include "presentation.hpp"
#include "picture_render.hpp"
#include "picture_geometry.hpp"
#include "controls.hpp"
#include <stb/stb_image.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>

void check_picture_transforms(SDL_Renderer* renderer) {
    std::vector<unsigned char> pixels(8 * 4 * 4, 255);
    const std::array<std::array<unsigned char, 3>, 4> colors{{{255,0,0}, {0,0,255}, {0,255,0}, {255,255,0}}};
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x)
        for (int c = 0; c < 3; ++c) pixels[(y * 8 + x) * 4 + c] = colors[(y >= 2) * 2 + (x >= 4)][c];
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 8, 4), SDL_DestroyTexture);
    if (!texture || SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), 32) != 0) throw std::runtime_error(SDL_GetError());
    SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
    for (int flip_y : {0, 1}) for (int flip_x : {0, 1}) for (int angle : {0, 90}) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); SDL_RenderClear(renderer);
        const wolf::Json picture{{"x",32}, {"y",24}, {"anchor",1}, {"zoom",flip_x ? -200 : 200}, {"zoom_y",flip_y ? -200 : 200}};
        const auto area = wolf::picture_area(picture, 8, 4);
        draw_picture_copy(renderer, texture.get(), nullptr,
            {static_cast<float>(area.x), static_cast<float>(area.y), static_cast<float>(area.w), static_cast<float>(area.h)}, angle);
        std::vector<unsigned char> output(64 * 56 * 4);
        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, output.data(), 64 * 4) != 0) throw std::runtime_error(SDL_GetError());
        // At 90 degrees the lower-left source corner becomes the upper-left.
        const int index = angle ? (flip_x ? 1 : 0) + (flip_y ? 0 : 2) : flip_x + flip_y * 2;
        const int sample = ((angle ? 18 : 22) * 64 + (angle ? 30 : 26)) * 4;
        for (int c = 0; c < 3; ++c) if (output[sample + c] != colors[index][c])
            throw std::runtime_error("Mirrored/rotated picture pixels do not match the expected corner");
    }
}

int main() {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* target = nullptr;
    try {
        const auto hidden = presentation::layout(2796, 1290, 1024, 576, {132,0,132,63}, false);
        const auto shown = presentation::layout(2796, 1290, 1024, 576, {132,0,132,63}, true);
        if (hidden.game.h <= shown.game.h || hidden.game.y + hidden.game.h > 1227.01f || hidden.controls.h != 0 ||
            std::abs(hidden.game.w / hidden.game.h - 1024.f / 576) > 0.001f) throw std::runtime_error("Hidden controls must enlarge the game without stretching or entering the home area");
        presentation::FloatingToggle toggle;
        auto dot = toggle.bounds(hidden.safe, 132);
        if (!toggle.press(hidden.safe, 132, dot.x + 20, dot.y + 20) || !toggle.release()) throw std::runtime_error("Three-dot tap did not toggle controls");
        if (!toggle.press(hidden.safe, 132, dot.x + 20, dot.y + 20)) throw std::runtime_error("Three-dot drag did not capture the pointer");
        toggle.move(hidden.safe, 132, -500, 5000, 24); dot = toggle.bounds(hidden.safe, 132);
        if (toggle.release() || dot.x < hidden.safe.x || dot.y + dot.h > hidden.safe.y + hidden.safe.h) throw std::runtime_error("Dragging the toggle must clamp to the safe area without acting as a tap");
        CancelButton cancel; wolf::Input pressed; pressed.cancel = true;
        const auto first = cancel.map(pressed), held = cancel.map(pressed); cancel.map({}); const auto again = cancel.map(pressed);
        if (!first.mouse_right_new || !first.mouse_right || held.mouse_right_new || !again.mouse_right_new) throw std::runtime_error("Cancel must act as one right-click edge per press");
        const auto safe = presentation::fit(2796, 1290, 1024, 656, {132, 0, 132, 63});
        if (safe.x < 132 || safe.x + safe.w > 2664 || safe.y < 0 || safe.y + safe.h > 1227.01f)
            throw std::runtime_error("iPhone presentation overlaps a notch or home gesture area");
        const int menu_x = presentation::coordinate(safe.x + safe.w * 15 / 16, safe.x, safe.w, 1024);
        const int menu_y = presentation::coordinate(safe.y + safe.h * 616 / 656, safe.y, safe.h, 656);
        if (!presentation::button(menu_x, menu_y, 7, 1024, 576) || presentation::button(menu_x, 700, 7, 1024, 576))
            throw std::runtime_error("Safe-area touch mapping does not reach Menu correctly");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("Screenshot test", 0, 0, 64, 56, SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error(SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 64, 56);
        if (!target || SDL_SetRenderTarget(renderer, target) != 0) throw std::runtime_error(SDL_GetError());
        check_picture_transforms(renderer);
        SDL_SetRenderDrawColor(renderer, 40, 110, 210, 255); SDL_RenderClear(renderer);
        SDL_Rect marker{4, 4, 12, 12};
        SDL_SetRenderDrawColor(renderer, 230, 30, 70, 255); SDL_RenderFillRect(renderer, &marker);
        std::vector<unsigned char> skin(3 * 9 * 4, 255);
        for (int y = 0; y < 9; ++y) for (int x = 0; x < 3; ++x) {
            const int at = (y * 3 + x) * 4;
            skin[at] = y < 3 ? 255 : 0; skin[at + 1] = y >= 3 && y < 6 ? 255 : 0; skin[at + 2] = y >= 6 ? 255 : 0;
        }
        auto* skin_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 3, 9);
        if (!skin_texture || SDL_UpdateTexture(skin_texture, nullptr, skin.data(), 12) != 0) throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(skin_texture, SDL_BLENDMODE_BLEND);
        draw_window_skin(renderer, skin_texture, 3, 9, SDL_FRect{40, 4, 12, 4}, 1, 1);
        SDL_DestroyTexture(skin_texture);
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
            if (std::string(name).find(".png") != std::string::npos) {
                const auto upper = (4 * width + 45) * 4, lower = (5 * width + 45) * 4;
                if (pixels.get()[upper] != 255 || pixels.get()[upper + 2] != 0 ||
                    pixels.get()[lower] != 0 || pixels.get()[lower + 2] != 255)
                    throw std::runtime_error("Short window corners did not overlap in their original draw order");
            }
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
