#pragma once
#include "play_tools.hpp"
#include <SDL.h>
#include <charconv>
#include <functional>
#include <optional>
#include <mutex>
#include <cstring>
#include <cstdlib>

#ifdef WOLF_IOS
extern "C" int wolf_ios_prompt_integer(SDL_Window*, const char*, int, int*);
extern "C" char* wolf_ios_prompt_text(SDL_Window*, const char*, const char*);
extern "C" void wolf_ios_wait_for_alert_dismissal(SDL_Window*);
#endif

namespace play_tools {
inline void finish_modal(SDL_Window* window) {
#ifdef WOLF_IOS
    wolf_ios_wait_for_alert_dismissal(window);
#else
    (void)window;
#endif
}
inline int choose(SDL_Window* window, const char* title, const std::string& message, const std::vector<std::string>& labels) {
    std::vector<SDL_MessageBoxButtonData> buttons;
    for (std::size_t i = 0; i < labels.size(); ++i)
        buttons.push_back({i == 0 ? static_cast<Uint32>(SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT) : 0u, static_cast<int>(i), labels[i].c_str()});
    SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, window, title, message.c_str(), static_cast<int>(buttons.size()), buttons.data(), nullptr};
    int result = 0;
    const bool success = SDL_ShowMessageBox(&box, &result) == 0;
    finish_modal(window); return success ? result : 0;
}
inline void notice(SDL_Window* window, const char* message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Game tools", message, window);
    finish_modal(window);
}
using PromptPaint = std::function<void(const std::string&)>;
inline std::optional<std::string> prompt_text(SDL_Window* window, const std::string& title, const std::string& initial, const PromptPaint& paint) {
#ifdef WOLF_IOS
    (void)paint;
    char* answer = wolf_ios_prompt_text(window, title.c_str(), initial.c_str());
    if (!answer) return {};
    std::string result(answer); std::free(answer); return result;
#else
    (void)window;
    std::string value = initial; bool replace = true;
    SDL_StartTextInput(); std::optional<std::string> result;
    for (;;) {
        paint(title + "\n\n" + value + "_\n\nEnter: accept. Escape: cancel.");
        SDL_Event event; if (!SDL_WaitEventTimeout(&event, 50)) continue;
        if (event.type == SDL_QUIT || event.type == SDL_APP_WILLENTERBACKGROUND) { SDL_PushEvent(&event); break; }
        if (event.type == SDL_TEXTINPUT) {
            if (replace) { value.clear(); replace = false; }
            if (value.size() + std::strlen(event.text.text) <= 160) value += event.text.text;
        }
        if (event.type != SDL_KEYDOWN) continue;
        if (event.key.keysym.sym == SDLK_ESCAPE) break;
        if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) { result = value; break; }
        if (event.key.keysym.sym == SDLK_BACKSPACE) {
            if (replace) value.clear();
            else if (!value.empty()) { auto pos = value.size() - 1; while (pos && (static_cast<unsigned char>(value[pos]) & 0xc0) == 0x80) --pos; value.resize(pos); }
            replace = false;
        }
    }
    SDL_StopTextInput(); return result;
#endif
}
inline std::optional<int> prompt_integer(SDL_Window* window, const std::string& title, int initial, const PromptPaint& paint) {
#ifdef WOLF_IOS
    (void)paint;
    int result = initial;
    if (wolf_ios_prompt_integer(window, title.c_str(), initial, &result) == 1) return result;
    return {};
#else
    (void)window;
    std::string value = std::to_string(initial); bool replace = true;
    SDL_StartTextInput();
    std::optional<int> result;
    bool done = false;
    while (!done) {
        paint(title + "\n\n" + value + "_\n\nType a whole number. Enter: set. Escape: cancel.");
        SDL_Event event;
        if (!SDL_WaitEventTimeout(&event, 50)) continue;
        if (event.type == SDL_QUIT || event.type == SDL_APP_WILLENTERBACKGROUND) { SDL_PushEvent(&event); break; }
        if (event.type == SDL_TEXTINPUT) {
            if (replace) { value.clear(); replace = false; }
            for (const char* c = event.text.text; *c && value.size() < 12; ++c)
                if ((*c >= '0' && *c <= '9') || (*c == '-' && value.empty())) value += *c;
        }
        if (event.type != SDL_KEYDOWN) continue;
        if (event.key.keysym.sym == SDLK_ESCAPE) break;
        if (event.key.keysym.sym == SDLK_BACKSPACE) { if (replace) value.clear(); else if (!value.empty()) value.pop_back(); replace = false; }
        if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
            int parsed = 0; const auto read = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (read.ec == std::errc{} && read.ptr == value.data() + value.size()) { result = parsed; done = true; }
        }
    }
    SDL_StopTextInput(); return result;
#endif
}

inline bool save_states_menu(SDL_Window* window, SaveStates& states, wolf::Runtime& runtime, Cursor& cursor, std::mutex& guard) {
    const int action = choose(window, "Save states", "Separate snapshots of the current moment. Normal save slots are kept.",
        {"Back", "Save state", "Load state"});
    if (!action) return false;
    const int slot = choose(window, action == 1 ? "Save state" : "Load state", "Choose a separate state slot.",
        {"Back", states.label(1), states.label(2), states.label(3)});
    if (!slot) return false;
    try {
        if (action == 1) {
            if (!runtime.error().empty()) { notice(window, "The game is stopped by an error. Load an earlier state instead."); return false; }
            if (states.exists(slot) && choose(window, "Replace state?", "Replace the snapshot in this state slot?", {"Cancel", "Replace"}) != 1) return false;
            { std::lock_guard<std::mutex> lock(guard); states.save(slot, runtime, cursor); }
            notice(window, "State saved.");
        } else {
            if (!states.exists(slot)) { notice(window, "This state slot is empty."); return false; }
            if (choose(window, "Load state?", "Replace current progress with this snapshot? Normal save files are kept.", {"Cancel", "Load"}) != 1) return false;
            { std::lock_guard<std::mutex> lock(guard); cursor = states.load(slot, runtime); }
            return true;
        }
    } catch (const std::exception& error) { notice(window, error.what()); }
    return false;
}

inline std::string pinned_hud(const Cheats& cheats, wolf::Runtime& runtime) {
    if (!cheats.enabled || !cheats.show_pins) return {};
    std::string text; int shown = 0;
    for (const auto& [ref, entry] : cheats.entries) if (entry.pinned) {
        if (shown++ == 6) { text += "\nMore pins in Cheats"; break; }
        if (!text.empty()) text += '\n';
        text += literal_text(cheats.label(ref)) + ": " + std::to_string(runtime.number(ref)) + (entry.frozen ? " [F]" : "");
    }
    return text;
}
}
