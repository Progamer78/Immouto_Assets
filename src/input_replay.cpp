#include "input_replay.hpp"
#include <stdexcept>

namespace wolf {
InputReplay::InputReplay(const Json& rows) {
    if (!rows.is_array()) throw std::runtime_error("Input replay must be an array");
    const std::map<std::string, bool Input::*> buttons = {{"up", &Input::up}, {"down", &Input::down},
        {"left", &Input::left}, {"right", &Input::right}, {"confirm", &Input::confirm},
        {"cancel", &Input::cancel}, {"sub", &Input::sub}, {"mouse_left", &Input::mouse_left},
        {"mouse_right", &Input::mouse_right}, {"mouse_middle", &Input::mouse_middle}};
    for (const auto& row : rows) {
        const auto frame = row.at("frame").get<std::int64_t>();
        if (frame < 0 || (!changes_.empty() && static_cast<std::uint64_t>(frame) <= changes_.back().first))
            throw std::runtime_error("Input replay frames must be nonnegative and strictly increasing");
        Input state;
        for (const auto& [key, value] : row.items()) {
            const auto button = buttons.find(key);
            if (button != buttons.end()) state.*button->second = value.get<bool>();
            else if (key == "mouse_x") state.mouse_x = value;
            else if (key == "mouse_y") state.mouse_y = value;
            else if (key == "wheel") state.wheel = value;
            else if (key != "frame") throw std::runtime_error("Unknown replay input: " + key);
        }
        changes_.emplace_back(static_cast<std::uint64_t>(frame), state);
    }
}
Input InputReplay::input(std::uint64_t frame) {
    const bool left = held_.mouse_left, right = held_.mouse_right;
    while (next_ < changes_.size() && changes_[next_].first <= frame) held_ = changes_[next_++].second;
    auto result = held_;
    result.mouse_left_new = result.mouse_left && !left;
    result.mouse_right_new = result.mouse_right && !right;
    held_.wheel = 0;
    return result;
}
}
