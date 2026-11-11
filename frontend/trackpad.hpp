#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Window-point gestures move a cursor in game coordinates. A drag only moves;
// a separate tap clicks. Short clicks last two ticks for held-state game menus.
class Trackpad {
    bool captured_ = false, dragged_ = false;
    float last_x_ = 0, last_y_ = 0, start_x_ = 0, start_y_ = 0;
    std::uint32_t began_ = 0;
    int left_ = 0, right_ = 0;
public:
    float x = 512, y = 288;
    bool captured() const { return captured_; }
    void reset() { captured_ = dragged_ = false; left_ = right_ = 0; }
    void press(float px, float py, std::uint32_t now) {
        captured_ = true; dragged_ = false; start_x_ = last_x_ = px; start_y_ = last_y_ = py; began_ = now;
    }
    void move(float px, float py, float game_per_point, int width, int height) {
        if (!captured_) return;
        dragged_ |= std::hypot(px - start_x_, py - start_y_) > 6;
        // The threshold distinguishes a click from a drag; it must not impose
        // a dead zone on the visible cursor or discard slow, precise movement.
        x = std::clamp(x + (px - last_x_) * game_per_point, 0.f, static_cast<float>(width - 1));
        y = std::clamp(y + (py - last_y_) * game_per_point, 0.f, static_cast<float>(height - 1));
        last_x_ = px; last_y_ = py;
    }
    void release(std::uint32_t now) {
        if (captured_ && !dragged_) {
            if (now - began_ >= 450) right_ = 2;
            else left_ = 2;
        }
        captured_ = false;
    }
    wolf::Input map(wolf::Input input) const {
        input.mouse_x = static_cast<int>(x); input.mouse_y = static_cast<int>(y);
        input.mouse_left = left_ > 0; input.mouse_left_new = left_ == 2;
        input.mouse_right = input.mouse_right || right_ > 0;
        input.mouse_right_new = input.mouse_right_new || right_ == 2;
        return input;
    }
    void tick() { left_ = std::max(0, left_ - 1); right_ = std::max(0, right_ - 1); }
};
