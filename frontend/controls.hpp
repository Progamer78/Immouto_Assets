#pragma once
#include "runtime.hpp"

// Custom game menus use mouse cancel even when basic keyboard input is disabled.
class CancelButton {
    bool held_ = false;
public:
    wolf::Input map(wolf::Input input) {
        const bool held = input.cancel;
        input.mouse_right |= held;
        input.mouse_right_new |= held && !held_;
        held_ = held;
        return input;
    }
};

// Retain a tap that begins and ends between simulation ticks. Custom WOLF
// menus distinguish the first press (2) from the following held tick (1).
class MouseClickLatch {
    int left_ = 0, right_ = 0;
public:
    void press(bool left) { (left ? left_ : right_) = 2; }
    void reset() { left_ = right_ = 0; }
    wolf::Input map(wolf::Input input) const {
        input.mouse_left |= left_ > 0; input.mouse_left_new |= left_ == 2;
        input.mouse_right |= right_ > 0; input.mouse_right_new |= right_ == 2;
        return input;
    }
    void tick() { if (left_) --left_; if (right_) --right_; }
};
