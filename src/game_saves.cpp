#include "runtime.hpp"
#include <algorithm>
#include <cmath>

namespace wolf {
void Runtime::return_to_title() { reset_to_title(); }

void Runtime::reset_to_title() {
    flush_saves();
    Runtime fresh(project_, save_root_, asset_root_, resource_root_, project_id_);
    fresh.frame_ = frame_ + 1;
    fresh.input_ = fresh.previous_input_ = input_; fresh.raw_input_ = raw_input_;
    fresh.set_number_freezes(number_freezes_);
    for (int kind = 0; kind < 3; ++kind)
        fresh.audio_.push_back({{"operation", "stop"}, {"kind", kind}, {"channel", -1}, {"fade_ms", 0}});
    *this = std::move(fresh);
}

void Runtime::game_save_load(Run& run, const Json& command) {
    const auto& args = command.at("args");
    if (args.size() != 2 || !command.at("strings").empty() || (args.at(0) != 0 && args.at(0) != 1))
        fail("Unsupported full save/load command layout");
    const auto name = save_name(args.at(1), run.stack.back().context);
    auto& slot = save_slot(name);
    if (args.at(0) == 0) {
        if (save_root_.empty()) fail("Game saving requires a persistent storage directory");
        const auto state = snapshot();
        slot = {{"format", "wolf-native-variables"}, {"version", 1}, {"project_id", project_id_},
            {"numbers", state.at("numbers")}, {"strings", state.at("strings")}, {"game", state}};
        dirty_saves_.insert(name);
        return;
    }
    if (!run.blocking && run.owner.compare(0, 7, "manual:") != 0)
        fail("Loading a game from a parallel event is not supported by the original engine");
    if (slot.is_null()) return;
    if (!slot.contains("game")) fail("This slot contains partial variables, not a native game save");
    pending_game_load_ = slot.at("game");
    // Commands 221/222 address the same saved variables as a complete save.
    pending_game_load_["numbers"] = slot.at("numbers");
    pending_game_load_["strings"] = slot.at("strings");
}

void Runtime::apply_game_load() {
    auto state = std::move(pending_game_load_);
    pending_game_load_ = Json::object();
    const auto saved_frame = state.at("frame").get<std::uint64_t>();
    const auto loaded_frame = std::max(frame_ + 1, saved_frame);
    const auto shift = loaded_frame - saved_frame;
    // Keep the interpreter clock monotonic while preserving animation ages.
    state["frame"] = loaded_frame;
    for (auto& picture : state.at("pictures")) {
        for (const char* effect : {"tween", "fade_erase", "flicker", "shake", "pattern_effect", "text_reveal", "zoom_effect", "color_correction", "flash"})
            if (picture.contains(effect) && picture.at(effect).contains("start"))
                picture[effect]["start"] = picture.at(effect).at("start").get<std::uint64_t>() + shift;
    }
    if (state.contains("music")) for (auto& music : state.at("music"))
        music["updated"] = music.at("updated").get<std::uint64_t>() + shift;
    // Unlike a development resume, WOLF Load restarts event scheduling rather
    // than continuing the saved call stacks, choices or reserved picture work.
    state["runs"] = state["routes"] = Json::array();
    state["pending_picture_changes"] = state["pending_picture_erases"] = state["pending_audio"] = Json::array();
    state["message"] = ""; state["message_state"] = state["choice"] = Json::object();
    state["choice_clear_after"] = 0; state["pending_auto_input"] = 0;
    state["numbers"]["9000012"] = 0; state["numbers"]["9000020"] = -1;
    const auto current_input = input_, current_raw = raw_input_;
    flush_saves();
    restore(state); // Validate in a candidate runtime before replacing live state.
    input_ = previous_input_ = current_input; raw_input_ = current_raw;
    auto restarted = std::move(audio_); audio_ = Json::array();
    for (int kind = 0; kind < 3; ++kind)
        audio_.push_back({{"operation", "stop"}, {"kind", kind}, {"channel", -1}, {"fade_ms", 0}});
    for (auto& event : restarted) audio_.push_back(std::move(event));
}

void Runtime::remember_music(const Json& event) {
    const auto key = std::to_string(event.at("kind").get<int>());
    double position = event.at("start_ms");
    if (music_.contains(key) && music_.at(key).at("event").at("file") == event.at("file")) {
        const auto& old = music_.at(key);
        position = old.at("position_ms").get<double>() +
            (frame_ - old.at("updated").get<std::uint64_t>()) * 1000.0 / project_->at("game").at("fps").get<int>() *
            old.at("event").at("tempo").get<double>() / 100.0;
    }
    music_[key] = {{"event", event}, {"position_ms", position}, {"updated", frame_}};
}

void Runtime::resume_music() {
    if (!music_.is_object()) fail("Invalid saved music state");
    for (const auto& [key, music] : music_.items()) {
        const auto& event = music.at("event");
        const auto updated = music.at("updated").get<std::uint64_t>();
        const double position = music.at("position_ms");
        if ((key != "0" && key != "1") || event.at("kind") != std::stoi(key) || event.at("operation") != "play" ||
            updated > frame_ || !std::isfinite(position) || position < 0 || event.at("tempo").get<int>() <= 0)
            fail("Invalid saved music playback metadata");
        auto resumed = event;
        resumed["resume_position_ms"] = position + (frame_ - updated) * 1000.0 / project_->at("game").at("fps").get<int>() *
            event.at("tempo").get<double>() / 100.0;
        resumed["fade_ms"] = 0;
        audio_.push_back(std::move(resumed));
    }
}
}
