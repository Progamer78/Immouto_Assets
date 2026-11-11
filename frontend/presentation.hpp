#pragma once
#include <algorithm>
#include <cmath>

namespace presentation {
constexpr int controls_height = 80;
constexpr int button_count = 10;
constexpr int a_button = 7, b_button = 8, menu_button = 9;
struct Insets { float left = 0, top = 0, right = 0, bottom = 0; };
struct Rect { float x = 0, y = 0, w = 0, h = 0; };
inline bool contains(Rect rect, float x, float y) { return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h; }
inline Rect safe_area(int width, int height, Insets insets) {
    const float left = std::clamp(insets.left, 0.f, width / 3.f), right = std::clamp(insets.right, 0.f, width / 3.f);
    const float top = std::clamp(insets.top, 0.f, height / 3.f), bottom = std::clamp(insets.bottom, 0.f, height / 3.f);
    return {left, top, std::max(1.f, width - left - right), std::max(1.f, height - top - bottom)};
}
struct Layout { Rect game, controls, safe; };
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
inline Layout layout(int output_w, int output_h, int width, int height, Insets insets, bool controls_visible) {
    const auto area = fit(output_w, output_h, width, height + (controls_visible ? controls_height : 0), insets);
    const float game_h = area.h * height / (height + (controls_visible ? controls_height : 0));
    return {{area.x, area.y, area.w, game_h}, controls_visible ? Rect{area.x, area.y + game_h, area.w, area.h - game_h} : Rect{},
        safe_area(output_w, output_h, insets)};
}
class FloatingToggle {
    bool captured_ = false, dragged_ = false;
    float start_x_ = 0, start_y_ = 0, offset_x_ = 0, offset_y_ = 0;
public:
    float x = 1, y = 0;
    Rect bounds(Rect safe, float size) const {
        size = std::min(size, std::min(safe.w, safe.h));
        return {safe.x + std::clamp(x, 0.f, 1.f) * (safe.w - size), safe.y + std::clamp(y, 0.f, 1.f) * (safe.h - size), size, size};
    }
    bool press(Rect safe, float size, float px, float py) {
        const auto rect = bounds(safe, size);
        if (!contains(rect, px, py)) return false;
        captured_ = true; dragged_ = false; start_x_ = px; start_y_ = py; offset_x_ = px - rect.x; offset_y_ = py - rect.y;
        return true;
    }
    void move(Rect safe, float size, float px, float py, float threshold) {
        if (!captured_) return;
        dragged_ |= std::hypot(px - start_x_, py - start_y_) > threshold;
        if (!dragged_) return;
        x = std::clamp((px - offset_x_ - safe.x) / std::max(1.f, safe.w - size), 0.f, 1.f);
        y = std::clamp((py - offset_y_ - safe.y) / std::max(1.f, safe.h - size), 0.f, 1.f);
    }
    bool release() { const bool tapped = captured_ && !dragged_; captured_ = false; return tapped; }
    bool captured() const { return captured_; }
};
}
