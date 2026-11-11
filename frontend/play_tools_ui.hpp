#pragma once
#include "play_tools.hpp"
#include <SDL.h>
#include <charconv>
#include <functional>
#include <optional>
#include <mutex>

#ifdef WOLF_IOS
extern "C" int wolf_ios_prompt_integer(SDL_Window*, const char*, int, int*);
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

inline void variable_menu(SDL_Window* window, Cheats& cheats, wolf::Runtime& runtime, int ref, const PromptPaint& paint, std::mutex& guard) {
    for (;;) {
        auto& entry = cheats.entries[ref];
        const auto value = runtime.number(ref);
        const auto action = choose(window, variable_name(ref).c_str(), "Variable " + std::to_string(ref - 2000000) + " = " + std::to_string(value),
            {"Back", "Set value", "Add 100", "Refill to maximum", entry.frozen ? "Unfreeze" : "Freeze current value", entry.pinned ? "Unpin" : "Pin"});
        if (!action) break;
        if (action == 1) { if (auto edited = prompt_integer(window, "Set " + variable_name(ref), value, paint)) { std::lock_guard<std::mutex> lock(guard); cheats.edit(runtime, ref, *edited); } }
        if (action == 2) { std::lock_guard<std::mutex> lock(guard); cheats.edit(runtime, ref, static_cast<int>(std::min<std::int64_t>(INT32_MAX, static_cast<std::int64_t>(value) + 100))); }
        if (action == 3) {
            int maximum = 0;
            for (const auto& variable : quick_variables) if (variable.ref == ref) maximum = variable.maximum;
            if (maximum) { std::lock_guard<std::mutex> lock(guard); cheats.edit(runtime, ref, runtime.number(maximum)); }
            else notice(window, "This variable has no known maximum. Use Set value.");
        }
        if (action == 4) { std::lock_guard<std::mutex> lock(guard); cheats.freeze(runtime, ref, !entry.frozen); }
        if (action == 5) entry.pinned = !entry.pinned;
        cheats.persist();
    }
}
inline void cheats_menu(SDL_Window* window, Cheats& cheats, wolf::Runtime& runtime, const PromptPaint& paint, std::mutex& guard) {
    try {
      for (;;) {
        if (!cheats.enabled) {
            if (choose(window, "Cheats: off", "Optional value editing and freezes. Make a separate save state before changing gameplay values.", {"Back", "Enable cheats"}) != 1) return;
            cheats.enabled = true;
            { std::lock_guard<std::mutex> lock(guard); cheats.apply(runtime); }
            cheats.persist();
        }
        const int action = choose(window, "Cheats", "Frozen values resist game writes. Pins can be shown while playing.",
            {"Back", "Money", "AP", "Vigor", "Battle score", "Pinned variables", "Find variable by number", cheats.show_pins ? "Hide pinned HUD" : "Show pinned HUD", "Disable cheats"});
        if (!action) return;
        if (action >= 1 && action <= 4) variable_menu(window, cheats, runtime, quick_variables[action - 1].ref, paint, guard);
        if (action == 5) {
            std::vector<int> pinned;
            for (const auto& [ref, entry] : cheats.entries) if (entry.pinned) pinned.push_back(ref);
            if (pinned.empty()) { notice(window, "No variables pinned. Open a variable and choose Pin."); continue; }
            std::size_t page = 0;
            for (;;) {
                std::vector<std::string> labels{"Back"};
                const auto end = std::min(pinned.size(), page + 5);
                for (auto i = page; i < end; ++i) labels.push_back(variable_name(pinned[i]) + " = " + std::to_string(runtime.number(pinned[i])));
                labels.push_back("Next page");
                const auto selected = choose(window, "Pinned variables", "Choose a value to edit, freeze or unpin.", labels);
                if (!selected) break;
                if (static_cast<std::size_t>(selected) == labels.size() - 1) { page = end == pinned.size() ? 0 : end; continue; }
                variable_menu(window, cheats, runtime, pinned[page + selected - 1], paint, guard);
            }
        }
        if (action == 6) {
            if (auto id = prompt_integer(window, "Game variable number (0-999999)", 7, paint)) {
                if (*id < 0 || *id > 999999) notice(window, "Variable number must be between 0 and 999999.");
                else variable_menu(window, cheats, runtime, 2000000 + *id, paint, guard);
            }
        }
        if (action == 7) cheats.show_pins = !cheats.show_pins;
        if (action == 8) { cheats.enabled = false; std::lock_guard<std::mutex> lock(guard); cheats.apply(runtime); }
        cheats.persist();
      }
    } catch (const std::exception& error) { notice(window, error.what()); }
}
inline std::string pinned_hud(const Cheats& cheats, wolf::Runtime& runtime) {
    if (!cheats.enabled || !cheats.show_pins) return {};
    std::string text; int shown = 0;
    for (const auto& [ref, entry] : cheats.entries) if (entry.pinned) {
        if (shown++ == 6) { text += "\nMore pins in Cheats"; break; }
        if (!text.empty()) text += '\n';
        text += variable_name(ref) + ": " + std::to_string(runtime.number(ref)) + (entry.frozen ? " [F]" : "");
    }
    return text;
}
}
