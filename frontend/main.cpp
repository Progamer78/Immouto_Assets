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
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Missing resource: " + path.u8string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
struct Texture {
    SDL_Texture* value = nullptr;
    int width = 0, height = 0;
    int logical_width = 0, logical_height = 0, border = 0;
    std::string revision;
    std::vector<unsigned char> pixels;
    ~Texture() { if (value) SDL_DestroyTexture(value); }
};
struct Assets {
    wolf::Resources resources;
    SDL_Renderer* renderer;
    std::map<std::string, std::unique_ptr<Texture>> textures;
    TextRasterizer text;
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
        return *slot;
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
        return *slot;
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
        return *slot;
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
        return *slot;
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
        return *slot;
    }
};

void draw_pictures(SDL_Renderer* renderer, Assets& assets, const Json& pictures, std::uint64_t frame,
    int first_id = std::numeric_limits<int>::min(), int end_id = std::numeric_limits<int>::max()) {
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
        const auto area = wolf::picture_area(p, width, height);
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

presentation::Rect display_area(SDL_Window* window, SDL_Renderer* renderer, int width, int height) {
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
    return presentation::fit(output_w, output_h, width, height, insets);
}
bool show_app_menu(SDL_Window* window) {
    const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Keep playing"},
        {0, 1, "Return to title"}};
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, window, "Game menu",
        "Return to the title screen? Unsaved progress will be lost. Your saved slots are kept.", 2, buttons, nullptr};
    int selected = 0;
    return SDL_ShowMessageBox(&box, &selected) == 0 && selected == 1;
}
void capture_frame(SDL_Renderer* renderer, const char* filename, const wolf::Runtime& runtime) {
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
    if (!surface || SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch) != 0 ||
        SDL_SaveBMP(surface.get(), filename) != 0) throw std::runtime_error(SDL_GetError());
    auto evidence = runtime.diagnostics(); evidence["picture_records"] = runtime.pictures();
    wolf::atomic_save(std::string(filename) + ".json", evidence);
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
        double last = static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency(), accumulator = 0;
        wolf::Input input;
        bool compatibility_notice = false;
        while (running && !runtime.quit_requested()) {
            input.mouse_left_new = input.mouse_right_new = false; input.wheel = 0;
            bool keyboard_menu = false;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_APP_WILLENTERBACKGROUND ||
                    (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
                    std::lock_guard<std::mutex> lock(lifecycle.guard);
                    if (runtime.error().empty()) wolf::atomic_save(save_path.u8string(), runtime.snapshot());
                    paused = true; accumulator = 0;
                }
                if (event.type == SDL_APP_DIDENTERFOREGROUND ||
                    (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)) paused = false;
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    input.mouse_left_new |= event.button.button == SDL_BUTTON_LEFT;
                    input.mouse_right_new |= event.button.button == SDL_BUTTON_RIGHT;
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
            const auto display = display_area(window, renderer, width, screen_height);
            int window_w = 0, window_h = 0, output_w = 0, output_h = 0;
            SDL_GetWindowSize(window, &window_w, &window_h); SDL_GetRendererOutputSize(renderer, &output_w, &output_h);
            input.mouse_x = presentation::coordinate(input.mouse_x * static_cast<float>(output_w) / std::max(1, window_w), display.x, display.w, width);
            input.mouse_y = presentation::coordinate(input.mouse_y * static_cast<float>(output_h) / std::max(1, window_h), display.y, display.h, screen_height);
            input.mouse_left = (buttons & SDL_BUTTON_LMASK) != 0;
            input.mouse_right = (buttons & SDL_BUTTON_RMASK) != 0;
            input.mouse_middle = (buttons & SDL_BUTTON_MMASK) != 0;
            const auto* keys = SDL_GetKeyboardState(nullptr);
            auto pad = [&](SDL_GameControllerButton button) { return controller && SDL_GameControllerGetButton(controller, button); };
            auto touch = [&](int index) { return input.mouse_left && presentation::button(input.mouse_x, input.mouse_y, index, width, height); };
            const bool open_menu = (touch(7) && input.mouse_left_new) || keyboard_menu;
            input.left = keys[SDL_SCANCODE_LEFT] || pad(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || touch(0);
            input.up = keys[SDL_SCANCODE_UP] || pad(SDL_CONTROLLER_BUTTON_DPAD_UP) || touch(1);
            input.down = keys[SDL_SCANCODE_DOWN] || pad(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || touch(2);
            input.right = keys[SDL_SCANCODE_RIGHT] || pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || touch(3);
            input.confirm = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_Z] || pad(SDL_CONTROLLER_BUTTON_A) || touch(4);
            input.cancel = keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_X] || pad(SDL_CONTROLLER_BUTTON_B) || touch(5);
            input.sub = keys[SDL_SCANCODE_LSHIFT] || pad(SDL_CONTROLLER_BUTTON_X) || touch(6);
            if (input.mouse_x < 0 || input.mouse_x >= width || input.mouse_y < 0 || input.mouse_y >= height) {
                input.mouse_left = input.mouse_right = input.mouse_middle = false;
                input.mouse_left_new = input.mouse_right_new = false;
                input.mouse_x = input.mouse_y = -1;
            }
            if (open_menu) {
                if (show_app_menu(window)) {
                    std::lock_guard<std::mutex> lock(lifecycle.guard);
                    runtime.return_to_title(); compatibility_notice = false;
                }
                input = {}; accumulator = 0;
            }
            const double now = static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency();
            if (!capture_path.empty()) accumulator = 1.0 / fps;
            else if (!paused) accumulator += std::min(now - last, 0.25);
            last = now;
            int steps = 0;
            while (runtime.error().empty() && accumulator >= 1.0 / fps && steps++ < 8) {
                { std::lock_guard<std::mutex> lock(lifecycle.guard); runtime.tick(replay ? replay->input(runtime.frame()) : input); }
                audio.process(runtime.take_audio(), {runtime.number(9000100), runtime.number(9000101), runtime.number(9000102)});
                accumulator -= 1.0 / fps;
                input.mouse_left_new = input.mouse_right_new = false; input.wheel = 0;
                if (runtime.file_operations_pending()) break;
            }
            if (steps >= 8 || !runtime.error().empty()) accumulator = 0;
            audio.pause(paused || !runtime.error().empty());
            if (SDL_SetRenderTarget(renderer, game_target->value) != 0) throw std::runtime_error(SDL_GetError());
            SDL_RenderSetLogicalSize(renderer, width, screen_height);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
            draw_pictures(renderer, assets, runtime.pictures(), runtime.frame(), std::numeric_limits<int>::min(), 100000);
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
            const SDL_FRect destination{display.x, display.y, display.w, display.h};
            if (SDL_RenderCopyF(renderer, game_target->value, nullptr, &destination) != 0) throw std::runtime_error(SDL_GetError());
            const bool captured = !capture_path.empty() && (runtime.frame() >= static_cast<std::uint64_t>(capture_target) || !runtime.error().empty());
            if (captured) capture_frame(renderer, capture_path.c_str(), runtime);
            SDL_RenderPresent(renderer);
            if (!runtime.error().empty() && !compatibility_notice) {
                wolf::atomic_save((storage / "last-error.json").u8string(), runtime.diagnostics());
                SDL_Log("Compatibility stop: %s", runtime.diagnostics().dump().c_str());
                if (capture_path.empty()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Compatibility stop",
                    (runtime.error() + "\nUse Menu to return to the title screen. Saved slots are kept.").c_str(), window);
                compatibility_notice = true;
                if (!capture_path.empty()) { result = 1; break; }
            }
            if (captured) break;
            SDL_Delay(paused ? 30 : 1);
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
