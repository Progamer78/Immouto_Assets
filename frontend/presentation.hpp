#pragma once
#include <algorithm>
#include <cmath>

namespace presentation {
constexpr int controls_height = 80;
constexpr int button_count = 8;
struct Insets { float left = 0, top = 0, right = 0, bottom = 0; };
struct Rect { float x = 0, y = 0, w = 0, h = 0; };
inline Rect fit(int output_w, int output_h, int logical_w, int logical_h, Insets insets = {}) {
    const float left = std::clamp(insets.left, 0.f, output_w / 3.f);
    const float right = std::clamp(insets.right, 0.f, output_w / 3.f);
    const float top = std::clamp(insets.top, 0.f, output_h / 3.f);
    const float bottom = std::clamp(insets.bottom, 0.f, output_h / 3.f);
    const float available_w = std::max(1.f, output_w - left - right);
    const float available_h = std::max(1.f, output_h - top - bottom);
    const float scale = std::min(available_w / logical_w, available_h / logical_h);
    return {left + (available_w - logical_w * scale) / 2, top + (available_h - logical_h * scale) / 2,
        logical_w * scale, logical_h * scale};
}
inline int coordinate(float position, float origin, float extent, int logical) {
    return static_cast<int>(std::floor((position - origin) * logical / extent));
}
inline bool button(int x, int y, int index, int width, int height) {
    return y >= height && y < height + controls_height && x >= index * width / button_count && x < (index + 1) * width / button_count;
}
}
