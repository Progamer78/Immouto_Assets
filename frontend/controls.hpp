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
