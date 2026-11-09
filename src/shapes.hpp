#pragma once
#include <string>
#include <vector>

namespace wolf {
bool builtin_shape(const std::string& content);
std::vector<unsigned char> shape_pixels(const std::string& content, int width, int height);
}
