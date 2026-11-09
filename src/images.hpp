#pragma once
#include <array>
#include <vector>
namespace wolf {
std::vector<unsigned char> adjust_image_color(const std::vector<unsigned char>& rgba, const std::array<double, 3>& percent);
}
