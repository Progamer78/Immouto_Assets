#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <stdexcept>

namespace wolf {
struct PictureArea { double x, y, w, h; };
inline PictureArea picture_area(const Json& picture, double width, double height) {
    PictureArea area{picture.at("x"), picture.at("y"), width * picture.at("zoom").get<double>() / 100,
        height * picture.at("zoom_y").get<double>() / 100};
    switch (picture.at("anchor").get<int>()) {
    case 0: break;
    case 1: area.x -= area.w / 2; area.y -= area.h / 2; break;
    case 2: area.y -= area.h; break;
    case 3: area.x -= area.w; break;
    case 4: area.x -= area.w; area.y -= area.h; break;
    case 5: area.x -= area.w / 2; break;
    case 6: area.x -= area.w / 2; area.y -= area.h; break;
    default: throw std::runtime_error("Unsupported picture anchor");
    }
    return area;
}
inline bool picture_area_contains(PictureArea area, double x, double y) {
    return x >= std::min(area.x, area.x + area.w) && x < std::max(area.x, area.x + area.w) &&
        y >= std::min(area.y, area.y + area.h) && y < std::max(area.y, area.y + area.h);
}
}
