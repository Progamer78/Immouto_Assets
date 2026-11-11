#include "runtime.hpp"
#include "asset_cache.hpp"
#include "crash_log.hpp"
#include "input_replay.hpp"
#include "audio.hpp"
#include "text.hpp"
#include "shapes.hpp"
#include "file_operations.hpp"
#include "images.hpp"
#include "window.hpp"
#include "presentation.hpp"
#include "controls.hpp"
#include "trackpad.hpp"
#include "performance.hpp"
#include "picture_geometry.hpp"
#include "picture_render.hpp"
#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <mutex>
#include <limits>
#include <cstdio>
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb/stb_image.h>

namespace fs = std::filesystem;
using wolf::Json;
#ifdef WOLF_IOS
extern "C" char* wolf_ios_documents_path();
extern "C" void wolf_ios_safe_area(SDL_Window*, float*, float*, float*, float*);
#endif

namespace {
struct Lifecycle {
    wolf::Runtime* runtime;
    fs::path save;
    std::atomic_bool paused{false};
    std::mutex guard;
    static int watch(void* userdata, SDL_Event* event) {
        auto& state = *static_cast<Lifecycle*>(userdata);
        if (event->type == SDL_APP_WILLENTERBACKGROUND) {
            state.paused.store(true);
            try {
                std::lock_guard<std::mutex> lock(state.guard);
                if (state.runtime->error().empty()) wolf::atomic_save(state.save.u8string(), state.runtime->snapshot());
            } catch (const std::exception& error) { SDL_Log("Background save failed: %s", error.what()); }
        } else if (event->type == SDL_APP_DIDENTERFOREGROUND) state.paused.store(false);
        return 1;
    }
    Lifecycle(wolf::Runtime* value, fs::path path) : runtime(value), save(std::move(path)) { SDL_AddEventWatch(watch, this); }
    ~Lifecycle() { SDL_DelEventWatch(watch, this); }
};
std::vector<unsigned char> read_bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Missing resource: " + path.u8string());
    const auto length = stream.tellg();
    if (length < 0 || length > 256 * 1024 * 1024) throw std::runtime_error("Invalid image resource size: " + path.u8string());
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read image resource: " + path.u8string());
    return bytes;
}
struct Texture {
    SDL_Texture* value = nullptr;
    int width = 0, height = 0;
    int logical_width = 0, logical_height = 0, border = 0;
    std::string revision;
    std::vector<unsigned char> pixels;
    std::uint64_t last_used = 0;
    std::size_t bytes() const { return pixels.size() + static_cast<std::size_t>(width) * height * 4; }
    ~Texture() { if (value) SDL_DestroyTexture(value); }
};
struct Assets {
    wolf::Resources resources;
    SDL_Renderer* renderer;
    std::map<std::string, std::unique_ptr<Texture>> textures;
    TextRasterizer text;
    std::uint64_t generation = 1;
    void trim() { trim_texture_cache(textures, 192u * 1024 * 1024, generation); }
    explicit Assets(fs::path path, SDL_Renderer* output, const Json& project, const fs::path& writable) : resources(path.u8string(), writable.u8string()), renderer(output),
        text(path, project.at("game"), project.at("databases").at("system").at("types").at(12).at("rows")) {}
    Texture& text_picture(const Json& picture, const std::string& cache_name = {}) {
        const auto key = std::string("text:") + picture.at("text_style").dump() + picture.at("content").get<std::string>();
        auto& slot = textures[cache_name.empty() ? key : cache_name];
        if (!slot || slot->revision != key) {
            auto pixels = text.render(picture.at("content"), picture.at("text_style"));
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.rgba.data(), pixels.width, pixels.height, 32, pixels.width * 4, SDL_PIXELFORMAT_RGBA32);
            if (!surface) throw std::runtime_error(SDL_GetError());
            auto texture = std::make_unique<Texture>(); texture->value = SDL_CreateTextureFromSurface(renderer, surface);
            texture->width = pixels.width; texture->height = pixels.height; SDL_FreeSurface(surface);
            texture->logical_width = pixels.logical_width; texture->logical_height = pixels.logical_height; texture->border = pixels.border;
            texture->revision = key;
            texture->pixels = std::move(pixels.rgba);
            if (!texture->value) throw std::runtime_error(SDL_GetError());
            slot = std::move(texture);
        }
        slot->last_used = generation; return *slot;
    }
    Texture& image(const std::string& filename) {
        auto& slot = textures[filename];
        if (!slot) {
            const auto bytes = read_bytes(resources.path("Data/" + filename));
            int width = 0, height = 0, channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
            if (!pixels) throw std::runtime_error("Image decoding failed: " + filename);
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
            if (!surface) { stbi_image_free(pixels); throw std::runtime_error(SDL_GetError()); }
            auto texture = std::make_unique<Texture>();
            texture->value = SDL_CreateTextureFromSurface(renderer, surface);
            texture->width = width; texture->height = height;
            texture->pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4); texture->revision = filename;
            SDL_FreeSurface(surface); stbi_image_free(pixels);
            if (!texture->value) throw std::runtime_error(SDL_GetError());
            slot = std::move(texture);
        }
        slot->last_used = generation; return *slot;
    }
    Texture& square() {
        auto& slot = textures[":square"];
        if (!slot) {
            auto texture = std::make_unique<Texture>(); texture->width = texture->height = 1;
            texture->value = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1, 1);
            const unsigned char white[] = {255, 255, 255, 255};
            texture->pixels.assign(white, white + 4); texture->revision = ":square";
            if (!texture->value || SDL_UpdateTexture(texture->value, nullptr, white, 4) != 0) throw std::runtime_error(SDL_GetError());
            slot = std::move(texture);
        }
        slot->last_used = generation; return *slot;
    }
    Texture& shape(const Json& picture) {
        const int width = picture.at("div_x"), height = picture.at("div_y");
        const std::string content = picture.at("content");
        const auto revision = content + ":" + std::to_string(width) + ":" + std::to_string(height);
        auto& slot = textures[":shape:" + std::to_string(picture.at("id").get<int>())];
        if (!slot || slot->revision != revision) {
            const auto pixels = wolf::shape_pixels(content, width, height);
            auto texture = std::make_unique<Texture>(); texture->width = width; texture->height = height; texture->revision = revision;
            texture->pixels = pixels;
            texture->value = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
            if (!texture->value || SDL_UpdateTexture(texture->value, nullptr, pixels.data(), width * 4) != 0) throw std::runtime_error(SDL_GetError());
            slot = std::move(texture);
        }
        slot->last_used = generation; return *slot;
    }
    Texture& brightened(const Texture& base, int id, const std::array<double, 3>& color) {
        const auto revision = base.revision + Json(color).dump();
        auto& slot = textures[":tint:" + std::to_string(id)];
        if (!slot || slot->revision != revision) {
            const auto pixels = wolf::adjust_image_color(base.pixels, color);
            auto texture = std::make_unique<Texture>(); texture->width = base.width; texture->height = base.height;
            texture->logical_width = base.logical_width; texture->logical_height = base.logical_height; texture->border = base.border;
            texture->revision = revision;
            texture->value = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, base.width, base.height);
            if (!texture->value || SDL_UpdateTexture(texture->value, nullptr, pixels.data(), base.width * 4) != 0) throw std::runtime_error(SDL_GetError());
            slot = std::move(texture);
        }
        slot->last_used = generation; return *slot;
    }
};

void draw_pictures(SDL_Renderer* renderer, Assets& assets, const Json& pictures, std::uint64_t frame,
    int first_id = std::numeric_limits<int>::min(), int end_id = std::numeric_limits<int>::max(), const Trackpad* pointer = nullptr) {
    std::vector<const Json*> ordered;
    for (const auto& item : pictures) ordered.push_back(&item);
    std::sort(ordered.begin(), ordered.end(), [](const Json* a, const Json* b) { return a->at("id").get<int>() < b->at("id").get<int>(); });
    for (const Json* ptr : ordered) {
        const auto& p = *ptr;
        if (p.at("id").get<int>() < first_id || p.at("id").get<int>() >= end_id) continue;
        const int kind = p.at("kind");
        if (kind == -1) continue; // Empty record allocated by a delayed show.
        if (kind > 4) throw std::runtime_error("Unsupported picture display type");
        if ((p.at("blend") != 3 && p.at("opacity").get<int>() <= 0) || p.at("zoom") == 0 || p.at("zoom_y") == 0) continue;
        const bool window = kind >= 3;
        const std::string content = p.at("content");
        const bool line = window && content == "<LINE>";
        const bool builtin = window && wolf::builtin_shape(content);
        const auto tint = wolf::picture_tint(p, frame);
        const bool brighten = std::any_of(tint.begin(), tint.end(), [](double channel) { return channel > 100; });
        auto* selected = &(line ? assets.square() : builtin ? assets.shape(p) : kind == 2 ? assets.text_picture(p, "$picture:" + std::to_string(p.at("id").get<int>())) : assets.image(content));
        if (brighten) selected = &assets.brightened(*selected, p.at("id"), tint);
        auto& texture = *selected;
        const int dx = p.at("div_x"), dy = p.at("div_y"), pattern = wolf::picture_pattern(p, frame);
        if ((!line && (dx < 1 || dy < 1)) || (kind < 2 && (pattern < 1 || pattern > dx * dy)))
            throw std::runtime_error("Invalid picture sprite-sheet pattern");
        const int width = window ? dx : kind == 2 ? texture.logical_width : texture.width / dx;
        const int height = window ? dy : kind == 2 ? texture.logical_height : texture.height / dy;
        SDL_Rect source{window ? 0 : ((pattern - 1) % dx) * width, window ? 0 : ((pattern - 1) / dx) * height,
            window ? texture.width : width, window ? texture.height : height};
        if (kind == 2) source = {0, 0, texture.width, texture.height};
        const float zoom = p.at("zoom").get<float>() / 100.f;
        const float zoom_y = p.at("zoom_y").get<float>() / 100.f;
        const auto [shift_x, shift_y] = wolf::picture_draw_offset(p, frame);
        auto area = wolf::picture_area(p, width, height);
        if (pointer && game_cursor_picture(p)) {
            area.x += pointer->x - p.at("x").get<double>();
            area.y += pointer->y - p.at("y").get<double>();
        }
        SDL_FRect destination{static_cast<float>(area.x + shift_x), static_cast<float>(area.y + shift_y),
            static_cast<float>(area.w), static_cast<float>(area.h)};
        if (kind == 2) {
            destination.x -= texture.border * zoom; destination.y -= texture.border * zoom_y;
            destination.w = texture.width * zoom; destination.h = texture.height * zoom_y;
        }
        const auto group = wolf::picture_group_zoom(p, frame);
        destination.x = group[0] + (destination.x - group[0]) * group[2];
        destination.y = group[1] + (destination.y - group[1]) * group[2];
        destination.w *= group[2]; destination.h *= group[2];
        if (group[2] == 0) continue;
        const int blend = p.at("blend");
        if (blend == 2 || blend > 3) throw std::runtime_error("Unsupported picture blend mode");
        if (SDL_SetTextureBlendMode(texture.value, blend == 1 ? SDL_BLENDMODE_ADD : blend == 3 ? SDL_BLENDMODE_MOD : SDL_BLENDMODE_BLEND) != 0)
            throw std::runtime_error(SDL_GetError());
        SDL_SetTextureAlphaMod(texture.value, static_cast<Uint8>(blend == 3 ? 255 : std::clamp(p.at("opacity").get<int>(), 0, 255)));
        const auto drawing_tint = brighten ? std::array<double, 3>{100, 100, 100} : tint;
        SDL_SetTextureColorMod(texture.value, static_cast<Uint8>(drawing_tint[0] * 255 / 100),
            static_cast<Uint8>(drawing_tint[1] * 255 / 100), static_cast<Uint8>(drawing_tint[2] * 255 / 100));
        if (line) {
            SDL_SetRenderDrawBlendMode(renderer, blend == 1 ? SDL_BLENDMODE_ADD : blend == 3 ? SDL_BLENDMODE_MOD : SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(std::clamp(tint[0] * 255 / 100, 0.0, 255.0)), static_cast<Uint8>(std::clamp(tint[1] * 255 / 100, 0.0, 255.0)),
                static_cast<Uint8>(std::clamp(tint[2] * 255 / 100, 0.0, 255.0)), static_cast<Uint8>(blend == 3 ? 255 : std::clamp(p.at("opacity").get<int>(), 0, 255)));
            if (SDL_RenderDrawLineF(renderer, destination.x, destination.y, destination.x + destination.w, destination.y + destination.h) != 0)
                throw std::runtime_error(SDL_GetError());
            continue;
        }
        if (window && !builtin) {
            const auto flip = normalize_picture_area(destination);
            draw_window_skin(renderer, texture.value, texture.width, texture.height, destination, std::abs(zoom) * group[2], std::abs(zoom_y) * group[2], flip);
            continue;
        }
        draw_picture_copy(renderer, texture.value, &source, destination, window ? 0 : p.at("angle").get<double>() / 10);
    }
}

void draw_message(SDL_Renderer* renderer, Assets& assets, const Json& message, int height) {
    if (message.empty() || message.at("done") == true || message.at("visible") == 0) return;
    auto style = message.at("style"); style["visible_chars"] = message.at("visible");
    const Json picture = {{"content", message.at("content")}, {"text_style", style}};
    auto& texture = assets.text_picture(picture, "$message");
    const int x = message.at("x"), y = message.at("y");
    SDL_FRect destination{static_cast<float>((x < 0 ? 8 : x) - texture.border),
        static_cast<float>((y < 0 ? height - style.at("size").get<int>() * 4 - 16 : y) - texture.border),
        static_cast<float>(texture.width), static_cast<float>(texture.height)};
    SDL_SetTextureBlendMode(texture.value, SDL_BLENDMODE_BLEND); SDL_SetTextureAlphaMod(texture.value, 255);
    SDL_SetTextureColorMod(texture.value, 255, 255, 255);
    if (SDL_RenderCopyF(renderer, texture.value, nullptr, &destination) != 0) throw std::runtime_error(SDL_GetError());
}

presentation::Layout display_area(SDL_Window* window, SDL_Renderer* renderer, int width, int height, bool controls_visible) {
    int output_w = 0, output_h = 0;
    if (SDL_GetRendererOutputSize(renderer, &output_w, &output_h) != 0 || output_w < 1 || output_h < 1)
        throw std::runtime_error("Cannot determine display dimensions");
    presentation::Insets insets;
#ifdef WOLF_IOS
    wolf_ios_safe_area(window, &insets.left, &insets.top, &insets.right, &insets.bottom);
    insets.left *= output_w; insets.right *= output_w;
    insets.top *= output_h; insets.bottom *= output_h;
#else
    (void)window;
#endif
    return presentation::layout(output_w, output_h, width, height, insets, controls_visible);
}
void draw_controls_toggle(SDL_Renderer* renderer, presentation::Rect rect) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    auto circle = [&](float x, float y, float radius) {
        for (int dy = -static_cast<int>(radius); dy <= static_cast<int>(radius); ++dy) {
            const float span = std::sqrt(std::max(0.f, radius * radius - dy * dy));
            SDL_RenderDrawLineF(renderer, x - span, y + dy, x + span, y + dy);
        }
    };
    SDL_SetRenderDrawColor(renderer, 35, 35, 35, 220);
    circle(rect.x + rect.w / 2, rect.y + rect.h / 2, rect.w / 2);
    SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
    const float r = rect.w / 19;
    for (int i = -1; i <= 1; ++i) {
        circle(rect.x + rect.w / 2 + i * rect.w / 5, rect.y + rect.h / 2, r);
    }
}
int show_app_menu(SDL_Window* window, bool mouse_mode, bool show_performance) {
    const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Keep playing"},
        {0, 1, "Return to title"}, {0, 2, mouse_mode ? "Use direct touch" : "Enable mouse mode"},
        {0, 3, show_performance ? "Hide FPS monitor" : "Show FPS monitor"}};
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, window, "Game menu",
        "Mouse mode: drag to move the cursor, tap to click, hold then release to right-click.", 4, buttons, nullptr};
    int selected = 0;
    if (SDL_ShowMessageBox(&box, &selected) != 0) return 0;
    if (selected == 1) {
        const SDL_MessageBoxButtonData confirm[] = {{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Keep playing"}, {0, 1, "Return to title"}};
        const SDL_MessageBoxData question{SDL_MESSAGEBOX_WARNING, window, "Return to title?",
            "Unsaved progress will be lost. Your saved slots are kept.", 2, confirm, nullptr};
        int answer = 0;
        if (SDL_ShowMessageBox(&question, &answer) != 0 || answer != 1) return 0;
    }
    return selected;
}
void capture_frame(SDL_Renderer* renderer, const char* filename, const wolf::Runtime& runtime,
    const FrameMetrics::Reading& timing, bool real_time) {
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
    if (!surface || SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch) != 0 ||
        SDL_SaveBMP(surface.get(), filename) != 0) throw std::runtime_error(SDL_GetError());
    auto evidence = runtime.diagnostics(); evidence["picture_records"] = runtime.pictures();
    evidence["frontend_performance"] = {{"real_time", real_time}, {"fps", timing.fps}, {"frame_ms", timing.frame_ms},
        {"cpu_ms", timing.work_ms}, {"game_ms", timing.game_ms}, {"peak_ms", timing.peak_ms}, {"slow_frames", timing.slow}};
    wolf::atomic_save(std::string(filename) + ".json", evidence);
}
void draw_performance(SDL_Renderer* renderer, Assets& assets, const std::string& content,
    const presentation::Layout& display, float density) {
    const Json label = {{"content", content}, {"text_style", {{"size", 15}, {"shadow", 0}, {"speed", 0}, {"antialias", 1}}}};
    auto& texture = assets.text_picture(label, "$performance");
    const float scale = display.game.w / 1024.f;
    const SDL_FRect background{display.safe.x + 6 * density, display.safe.y + 6 * density,
        texture.width * scale + 12 * density, texture.height * scale + 8 * density};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(renderer, 0, 0, 0, 205);
    SDL_RenderFillRectF(renderer, &background);
    const SDL_FRect destination{background.x + 6 * density, background.y + 4 * density, texture.width * scale, texture.height * scale};
    SDL_SetTextureBlendMode(texture.value, SDL_BLENDMODE_BLEND); SDL_SetTextureColorMod(texture.value, 220, 255, 220); SDL_SetTextureAlphaMod(texture.value, 255);
    SDL_RenderCopyF(renderer, texture.value, nullptr, &destination);
}
}

int main(int argc, char** argv) {
#ifdef SDL_MAIN_HANDLED
    SDL_SetMainReady();
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) { SDL_Log("%s", SDL_GetError()); return 2; }
    int result = 0;
    SDL_Window* window = nullptr; SDL_Renderer* renderer = nullptr; SDL_GameController* controller = nullptr;
    std::unique_ptr<wolf::Runtime> active_runtime;
    std::unique_ptr<CrashLog> crash_log;
    fs::path storage;
    try {
#ifdef WOLF_IOS
        char* pref = wolf_ios_documents_path();
#else
        char* pref = SDL_GetPrefPath("MonochromePort", "MonochromePort");
#endif
        if (const char* override_path = SDL_getenv("WOLF_STORAGE_DIR")) { SDL_free(pref); pref = SDL_strdup(override_path); }
        if (!pref) throw std::runtime_error("Persistent storage unavailable");
        storage = fs::u8path(pref); SDL_free(pref);
        crash_log = std::make_unique<CrashLog>(storage / "crashes.txt");
        fs::path game;
        if (argc > 1) game = fs::u8path(argv[1]);
        else {
            char* base = SDL_GetBasePath();
            game = fs::u8path(base ? base : ".") / "game"; SDL_free(base);
        }
#ifdef WOLF_PERSIST_ASSETS
        // Show progress before copying the first full build's assets. Later thin updates skip the copy.
        window = SDL_CreateWindow("Preparing game", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 640, SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) throw std::runtime_error(SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(renderer, 1024, 640);
        Uint32 last_progress = 0;
        game = wolf::resolve_cached_game(game, storage, [&](std::size_t done, std::size_t total) {
            const auto now = SDL_GetTicks();
            if (now - last_progress < 16 && done != total) return;
            last_progress = now;
            SDL_PumpEvents();
            SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255); SDL_RenderClear(renderer);
            SDL_Rect bar{64, 306, static_cast<int>(896 * done / std::max<std::size_t>(1, total)), 28};
            SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255); SDL_RenderFillRect(renderer, &bar); SDL_RenderPresent(renderer);
        });
#endif
        active_runtime = std::make_unique<wolf::Runtime>(wolf::load_json((game / "game.json").u8string()), (storage / "saves").u8string(), game.u8string(), storage.u8string());
        auto& runtime = *active_runtime;
        const int width = runtime.project().at("game").at("width"), height = runtime.project().at("game").at("height");
        const int fps = runtime.project().at("game").at("fps");
        const char* capture_env = SDL_getenv("WOLF_CAPTURE_BMP");
        const std::string capture_path = capture_env ? capture_env : "";
        const char* realtime_env = SDL_getenv("WOLF_CAPTURE_REALTIME");
        const bool real_time = capture_path.empty() || (realtime_env && std::string(realtime_env) == "1");
        const char* capture_at = SDL_getenv("WOLF_CAPTURE_FRAME");
        const int capture_target = capture_at ? SDL_atoi(capture_at) : 0;
        if (!capture_path.empty() && capture_target < 1) throw std::runtime_error("WOLF_CAPTURE_FRAME must be positive when capturing");
        const char* replay_path = SDL_getenv("WOLF_REPLAY_INPUT");
        std::unique_ptr<wolf::InputReplay> replay;
        if (replay_path) replay = std::make_unique<wolf::InputReplay>(wolf::load_json(replay_path));
        const std::string title = runtime.project().at("game").at("title");
        const int screen_height = height + presentation::controls_height;
        if (!window) window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, screen_height,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (!capture_path.empty() ? SDL_WINDOW_HIDDEN : 0));
        if (!window) throw std::runtime_error(SDL_GetError());
        SDL_SetWindowTitle(window, title.c_str());
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        Assets assets(game, renderer, runtime.project(), storage);
        wolf::Resources writable(storage.u8string());
        auto game_target = std::make_unique<Texture>();
        game_target->value = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, screen_height);
        if (!game_target->value) throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(game_target->value, SDL_BLENDMODE_NONE);
        Audio audio(game);
        for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) { controller = SDL_GameControllerOpen(i); break; }
        const auto save_path = storage / "resume.json";
        // Ordinary launches use the game's title/Continue flow. Keep old
        // developer snapshots as diagnostics; opt in only for replay captures.
        const char* resume_env = SDL_getenv("WOLF_DEV_RESUME");
        const bool development_resume = resume_env ? std::string(resume_env) == "1" : !capture_path.empty();
        if (development_resume && fs::exists(save_path)) {
            try { runtime.restore(wolf::load_json(save_path.u8string())); }
            catch (const std::exception& error) {
                const auto rejected = storage / ("resume-rejected-" + std::to_string(SDL_GetPerformanceCounter()) + ".json");
                fs::rename(save_path, rejected);
                wolf::atomic_save((storage / "resume-rejection.json").u8string(), {{"reason", error.what()}, {"preserved", rejected.filename().u8string()}});
                SDL_Log("Preserved incompatible development resume: %s", error.what());
            }
        }
        Lifecycle lifecycle(&runtime, save_path);
        auto& paused = lifecycle.paused;
        bool running = true;
        auto seconds = [] { return static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency(); };
        double last = seconds(), accumulator = 0;
        FramePacer pacer(fps);
        FrameMetrics metrics;
        double previous_frame = 0, previous_work = 0, previous_game = 0, monitor_update = 0;
        std::string monitor_text = "Measuring frame timing...";
        wolf::Input input;
        CancelButton cancel_button;
        MouseClickLatch direct_click;
        Trackpad trackpad; trackpad.x = width / 2.f; trackpad.y = height / 2.f;
        bool pad_touch_active = false;
        SDL_FingerID pad_finger = 0;
        presentation::FloatingToggle controls_toggle;
        bool controls_visible = false, mouse_mode = false, show_performance = false;
        const auto controls_settings = storage / "controls.json";
        if (fs::exists(controls_settings)) try {
            const auto settings = wolf::load_json(controls_settings.u8string());
            controls_visible = settings.value("visible", false);
            mouse_mode = settings.value("mouse_mode", false);
            show_performance = settings.value("show_performance", false);
            controls_toggle.x = std::clamp(settings.value("x", 1.f), 0.f, 1.f);
            controls_toggle.y = std::clamp(settings.value("y", 0.f), 0.f, 1.f);
        } catch (...) { SDL_Log("Using default touch-control settings"); }
        auto save_controls = [&] {
            try { wolf::atomic_save(controls_settings.u8string(), {{"visible", controls_visible}, {"x", controls_toggle.x}, {"y", controls_toggle.y}, {"mouse_mode", mouse_mode}, {"show_performance", show_performance}}); }
            catch (...) { SDL_Log("Could not save touch-control settings"); }
        };
        bool compatibility_notice = false;
        while (running && !runtime.quit_requested()) {
            if (real_time && !paused) {
                double remaining;
                while ((remaining = pacer.remaining(seconds())) > 0)
                    SDL_Delay(remaining >= .002 ? static_cast<Uint32>((remaining - .001) * 1000) : 0);
            }
            const double frame_start = seconds();
            pacer.begin(frame_start);
            if (previous_frame) metrics.add(frame_start - previous_frame, previous_work, previous_game);
            previous_frame = frame_start;
            bool keyboard_menu = false;
            bool ui_mouse = controls_toggle.captured();
            const auto event_display = display_area(window, renderer, width, height, controls_visible);
            int event_window_w = 0, event_window_h = 0, event_output_w = 0, event_output_h = 0;
            SDL_GetWindowSize(window, &event_window_w, &event_window_h); SDL_GetRendererOutputSize(renderer, &event_output_w, &event_output_h);
            const float density = static_cast<float>(event_output_w) / std::max(1, event_window_w);
            const float toggle_size = 44 * density;
            auto pointer = [&](int x, int y) { return std::pair<float, float>{x * density, y * static_cast<float>(event_output_h) / std::max(1, event_window_h)}; };
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_APP_WILLENTERBACKGROUND ||
                    (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
                    std::lock_guard<std::mutex> lock(lifecycle.guard);
                    if (runtime.error().empty()) wolf::atomic_save(save_path.u8string(), runtime.snapshot());
                    paused = true; accumulator = 0;
                    trackpad.reset();
                    pad_touch_active = false;
                    direct_click.reset();
                }
                if (event.type == SDL_APP_DIDENTERFOREGROUND ||
                    (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)) {
                    paused = false; runtime.refresh_resources();
                    last = seconds(); accumulator = 0; pacer.reset(); metrics.reset(); previous_frame = 0;
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    const auto [px, py] = pointer(event.button.x, event.button.y);
                    const bool overlay = event.button.button == SDL_BUTTON_LEFT && controls_toggle.press(event_display.safe, toggle_size, px, py);
                    ui_mouse |= overlay;
                    const bool pad_press = mouse_mode && !overlay && event.button.button == SDL_BUTTON_LEFT && presentation::contains(event_display.game, px, py);
                    if (pad_press) trackpad.press(event.button.x, event.button.y, SDL_GetTicks());
                    if (!mouse_mode && !overlay && presentation::contains(event_display.game, px, py) &&
                        (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) direct_click.press(event.button.button == SDL_BUTTON_LEFT);
                    input.mouse_left_new |= event.button.button == SDL_BUTTON_LEFT && !overlay && !pad_press;
                    input.mouse_right_new |= event.button.button == SDL_BUTTON_RIGHT;
                }
                // SDL's synthesized mouse positions are integer window points
                // (three physical pixels per point on the user's iPhone).
                // Track a touch through its float coordinates, without also
                // applying its duplicate synthesized mouse motion.
                if (event.type == SDL_FINGERDOWN && mouse_mode && trackpad.captured() && !pad_touch_active) {
                    pad_finger = event.tfinger.fingerId; pad_touch_active = true;
                    trackpad.press(event.tfinger.x * event_window_w, event.tfinger.y * event_window_h, SDL_GetTicks());
                }
                if (event.type == SDL_FINGERMOTION && pad_touch_active && event.tfinger.fingerId == pad_finger)
                    trackpad.move(event.tfinger.x * event_window_w, event.tfinger.y * event_window_h,
                        width * density / event_display.game.w, width, height);
                if (event.type == SDL_FINGERUP && pad_touch_active && event.tfinger.fingerId == pad_finger) {
                    trackpad.move(event.tfinger.x * event_window_w, event.tfinger.y * event_window_h,
                        width * density / event_display.game.w, width, height);
                    trackpad.release(SDL_GetTicks()); pad_touch_active = false;
                }
                if (event.type == SDL_MOUSEMOTION && event.motion.which != SDL_TOUCH_MOUSEID && trackpad.captured())
                    trackpad.move(event.motion.x, event.motion.y, width * density / event_display.game.w, width, height);
                if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && trackpad.captured() &&
                    !(event.button.which == SDL_TOUCH_MOUSEID && pad_touch_active))
                    trackpad.release(SDL_GetTicks());
                if (event.type == SDL_MOUSEMOTION && controls_toggle.captured()) {
                    const auto [px, py] = pointer(event.motion.x, event.motion.y);
                    controls_toggle.move(event_display.safe, toggle_size, px, py, 8 * density); ui_mouse = true;
                }
                if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && controls_toggle.captured()) {
                    if (controls_toggle.release()) controls_visible = !controls_visible;
                    ui_mouse = true;
                    save_controls();
                }
                if (event.type == SDL_MOUSEWHEEL) input.wheel += event.wheel.y;
                if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_F12 && !event.key.repeat) keyboard_menu = true;
                if (event.type == SDL_CONTROLLERDEVICEADDED && !controller && SDL_IsGameController(event.cdevice.which))
                    controller = SDL_GameControllerOpen(event.cdevice.which);
                if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller &&
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                    SDL_GameControllerClose(controller); controller = nullptr;
                }
            }
            const auto buttons = SDL_GetMouseState(&input.mouse_x, &input.mouse_y);
            const auto display = display_area(window, renderer, width, height, controls_visible);
            int window_w = 0, window_h = 0, output_w = 0, output_h = 0;
            SDL_GetWindowSize(window, &window_w, &window_h); SDL_GetRendererOutputSize(renderer, &output_w, &output_h);
            const float pointer_x = input.mouse_x * static_cast<float>(output_w) / std::max(1, window_w);
            const float pointer_y = input.mouse_y * static_cast<float>(output_h) / std::max(1, window_h);
            input.mouse_x = presentation::coordinate(pointer_x, display.game.x, display.game.w, width);
            input.mouse_y = presentation::coordinate(pointer_y, display.game.y, display.game.h, height);
            input.mouse_left = (buttons & SDL_BUTTON_LMASK) != 0;
            input.mouse_right = (buttons & SDL_BUTTON_RMASK) != 0;
            input.mouse_middle = (buttons & SDL_BUTTON_MMASK) != 0;
            const auto* keys = SDL_GetKeyboardState(nullptr);
            auto pad = [&](SDL_GameControllerButton button) { return controller && SDL_GameControllerGetButton(controller, button); };
            auto touch = [&](int index) { return !ui_mouse && !trackpad.captured() && input.mouse_left && presentation::contains(display.controls, pointer_x, pointer_y) &&
                pointer_x >= display.controls.x + index * display.controls.w / 8 && pointer_x < display.controls.x + (index + 1) * display.controls.w / 8; };
            const bool open_menu = (touch(7) && input.mouse_left_new) || keyboard_menu;
            input.left = keys[SDL_SCANCODE_LEFT] || pad(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || touch(0);
            input.up = keys[SDL_SCANCODE_UP] || pad(SDL_CONTROLLER_BUTTON_DPAD_UP) || touch(1);
            input.down = keys[SDL_SCANCODE_DOWN] || pad(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || touch(2);
            input.right = keys[SDL_SCANCODE_RIGHT] || pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || touch(3);
            input.confirm = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_Z] || pad(SDL_CONTROLLER_BUTTON_A) || touch(4);
            input.cancel = keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_X] || pad(SDL_CONTROLLER_BUTTON_B) || touch(5);
            input.sub = keys[SDL_SCANCODE_LSHIFT] || pad(SDL_CONTROLLER_BUTTON_X) || touch(6);
            if (ui_mouse || input.mouse_x < 0 || input.mouse_x >= width || input.mouse_y < 0 || input.mouse_y >= height) {
                input.mouse_left = input.mouse_right = input.mouse_middle = false;
                input.mouse_left_new = input.mouse_right_new = false;
                input.mouse_x = input.mouse_y = -1;
            }
            if (open_menu) {
                const int selected = show_app_menu(window, mouse_mode, show_performance);
                if (selected == 1) {
                    std::lock_guard<std::mutex> lock(lifecycle.guard);
                    runtime.return_to_title(); compatibility_notice = false;
                }
                if (selected == 2) { mouse_mode = !mouse_mode; save_controls(); }
                if (selected == 3) { show_performance = !show_performance; save_controls(); }
                trackpad.reset();
                pad_touch_active = false;
                direct_click.reset();
                input = {}; accumulator = 0; last = seconds(); pacer.reset(); metrics.reset(); previous_frame = 0; monitor_update = 0;
            }
            if (paused) {
                audio.pause(true); accumulator = 0; last = seconds(); pacer.reset(); metrics.reset(); previous_frame = 0;
                SDL_Delay(30); continue;
            }
            // Simulation time follows the frame boundary. Variable event-pump
            // time must not turn steady frames into alternating zero/two ticks.
            const double now = std::max(frame_start, last);
            if (!real_time) accumulator = 1.0 / fps;
            else if (!paused) accumulator += std::min(now - last, 0.25);
            last = now;
            int steps = 0;
            const double game_start = seconds();
            while (runtime.error().empty() && accumulator >= 1.0 / fps && steps++ < 8) {
                { std::lock_guard<std::mutex> lock(lifecycle.guard);
                    runtime.tick(cancel_button.map(replay ? replay->input(runtime.frame()) : mouse_mode ? trackpad.map(input) :
                        input.mouse_x >= 0 && input.mouse_y >= 0 ? direct_click.map(input) : input)); }
                trackpad.tick();
                direct_click.tick();
                audio.process(runtime.take_audio(), {runtime.number(9000100), runtime.number(9000101), runtime.number(9000102)});
                accumulator -= 1.0 / fps;
                input.mouse_left_new = input.mouse_right_new = false; input.wheel = 0;
                if (runtime.file_operations_pending()) break;
            }
            previous_game = seconds() - game_start;
            if (steps >= 8 || !runtime.error().empty()) accumulator = 0;
            audio.pause(paused || !runtime.error().empty());
            ++assets.generation;
            if (SDL_SetRenderTarget(renderer, game_target->value) != 0) throw std::runtime_error(SDL_GetError());
            SDL_RenderSetLogicalSize(renderer, width, screen_height);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
            draw_pictures(renderer, assets, runtime.pictures(), runtime.frame(), std::numeric_limits<int>::min(), 100000, mouse_mode ? &trackpad : nullptr);
            draw_message(renderer, assets, runtime.message_state(), height);
            draw_pictures(renderer, assets, runtime.choice_pictures(), runtime.frame());
            draw_pictures(renderer, assets, runtime.pictures(), runtime.frame(), 100000);
            const auto file_operations = runtime.take_file_operations();
            if (!file_operations.empty()) {
                process_file_operations(renderer, writable, file_operations, width, height);
                assets.textures.clear(); assets.resources.invalidate();
            }
            for (int i = 0; i < presentation::button_count; ++i) {
                SDL_Rect rect{i * width / presentation::button_count + 3, height + 3, width / presentation::button_count - 6, presentation::controls_height - 6};
                SDL_SetRenderDrawColor(renderer, 56, 56, 56, 255); SDL_RenderFillRect(renderer, &rect);
                // Control glyphs use simple line geometry so no replacement font is bundled.
                const int cx = rect.x + rect.w / 2, cy = rect.y + rect.h / 2;
                SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
                if (i < 4) {
                    const int dx[] = {-1, 0, 0, 1}, dy[] = {0, -1, 1, 0};
                    SDL_RenderDrawLine(renderer, cx - dx[i] * 12, cy - dy[i] * 12, cx + dx[i] * 12, cy + dy[i] * 12);
                    SDL_RenderDrawLine(renderer, cx + dx[i] * 12, cy + dy[i] * 12, cx + dy[i] * 8, cy - dx[i] * 8);
                    SDL_RenderDrawLine(renderer, cx + dx[i] * 12, cy + dy[i] * 12, cx - dy[i] * 8, cy + dx[i] * 8);
                } else if (i == 4) {
                    SDL_RenderDrawLine(renderer, cx - 10, cy, cx - 2, cy + 8); SDL_RenderDrawLine(renderer, cx - 2, cy + 8, cx + 12, cy - 10);
                } else if (i == 5) {
                    SDL_RenderDrawLine(renderer, cx - 9, cy - 9, cx + 9, cy + 9); SDL_RenderDrawLine(renderer, cx - 9, cy + 9, cx + 9, cy - 9);
                } else {
                    const Json label = {{"content", i == 6 ? "Sub" : "Menu"}, {"text_style", {{"size", 20}, {"shadow", 0}, {"speed", 0}, {"antialias", 1}}}};
                    auto& text = assets.text_picture(label, i == 6 ? "$control:sub" : "$control:menu");
                    SDL_SetTextureBlendMode(text.value, SDL_BLENDMODE_BLEND); SDL_SetTextureColorMod(text.value, 230, 230, 230); SDL_SetTextureAlphaMod(text.value, 255);
                    const SDL_FRect area{cx - text.width / 2.f, cy - text.height / 2.f, static_cast<float>(text.width), static_cast<float>(text.height)};
                    SDL_RenderCopyF(renderer, text.value, nullptr, &area);
                }
            }
            if (SDL_SetRenderTarget(renderer, nullptr) != 0) throw std::runtime_error(SDL_GetError());
            SDL_RenderSetLogicalSize(renderer, 0, 0);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
            const SDL_Rect game_source{0, 0, width, height};
            const SDL_FRect destination{display.game.x, display.game.y, display.game.w, display.game.h};
            if (SDL_RenderCopyF(renderer, game_target->value, &game_source, &destination) != 0) throw std::runtime_error(SDL_GetError());
            if (controls_visible) {
                const SDL_Rect controls_source{0, height, width, presentation::controls_height};
                const SDL_FRect controls_destination{display.controls.x, display.controls.y, display.controls.w, display.controls.h};
                if (SDL_RenderCopyF(renderer, game_target->value, &controls_source, &controls_destination) != 0) throw std::runtime_error(SDL_GetError());
            }
            const auto& pictures = runtime.pictures();
            const bool game_pointer_visible = pictures.contains("10000") && game_cursor_picture(pictures.at("10000")) &&
                pictures.at("10000").at("opacity").get<int>() > 0 && pictures.at("10000").at("zoom") != 0 && pictures.at("10000").at("zoom_y") != 0;
            if (mouse_mode && !game_pointer_visible) {
                const float cx = display.game.x + trackpad.x * display.game.w / width, cy = display.game.y + trackpad.y * display.game.h / height;
                const float size = 14 * density;
                const SDL_FPoint arrow[] = {{cx,cy}, {cx + size * .2f,cy + size}, {cx + size * .45f,cy + size * .6f}, {cx + size * .85f,cy + size * .6f}, {cx,cy}};
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    SDL_FPoint outline[5]; for (int i = 0; i < 5; ++i) outline[i] = {arrow[i].x + dx * density, arrow[i].y + dy * density};
                    SDL_RenderDrawLinesF(renderer, outline, 5);
                }
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); SDL_RenderDrawLinesF(renderer, arrow, 5);
            }
            draw_controls_toggle(renderer, controls_toggle.bounds(display.safe, toggle_size));
            if (show_performance) {
                if (now >= monitor_update) {
                    const auto reading = metrics.read(fps);
                    char text[220];
                    std::snprintf(text, sizeof(text), "%.1f FPS  %.1f ms\nCPU %.1f ms  Game %.1f ms\nPeak %.1f ms  Slow %d", reading.fps, reading.frame_ms,
                        reading.work_ms, reading.game_ms, reading.peak_ms, reading.slow);
                    monitor_text = text; monitor_update = now + .25;
                }
                draw_performance(renderer, assets, monitor_text, display, density);
            }
            const bool captured = !capture_path.empty() && (runtime.frame() >= static_cast<std::uint64_t>(capture_target) || !runtime.error().empty());
            if (captured) capture_frame(renderer, capture_path.c_str(), runtime, metrics.read(fps), real_time);
            previous_work = seconds() - frame_start;
            SDL_RenderPresent(renderer);
            if (assets.generation % 60 == 0) assets.trim();
            if (!runtime.error().empty() && !compatibility_notice) {
                wolf::atomic_save((storage / "last-error.json").u8string(), runtime.diagnostics());
                SDL_Log("Compatibility stop: %s", runtime.diagnostics().dump().c_str());
                if (capture_path.empty()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Compatibility stop",
                    (runtime.error() + "\nUse Menu to return to the title screen. Saved slots are kept.").c_str(), window);
                compatibility_notice = true;
                if (!capture_path.empty()) { result = 1; break; }
            }
            if (captured) break;
        }
        if (runtime.error().empty()) {
            std::lock_guard<std::mutex> lock(lifecycle.guard);
            wolf::atomic_save(save_path.u8string(), runtime.snapshot());
        }
    } catch (const std::exception& error) {
        SDL_Log("%s", error.what());
        if (!storage.empty()) {
            try {
                Json diagnostic = active_runtime ? active_runtime->diagnostics() : Json::object();
                diagnostic["frontend_error"] = error.what();
                wolf::atomic_save((storage / "last-error.json").u8string(), diagnostic);
            } catch (...) { SDL_Log("Could not persist the error diagnostic"); }
        }
        if (!SDL_getenv("WOLF_CAPTURE_BMP")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Monochrome Port", error.what(), window);
        result = 2;
    }
    if (controller) SDL_GameControllerClose(controller);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit(); return result;
}
