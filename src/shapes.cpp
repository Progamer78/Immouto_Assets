#include "shapes.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <regex>
#include <stdexcept>

namespace wolf {
bool builtin_shape(const std::string& content) {
    static const std::regex patterned("<GRAD[XY]-[0-9]{3}-[0-9]{3}>|<TRI-(UP|DOWN|LEFT|RIGHT)>(FRAME|<FRAME>)?");
    return content == "<SQUARE>" || content == "<SQUARE>FRAME" || content == "<SQUARE><FRAME>" ||
        content == "<CIRCLE>" || content == "<CIRCLE>FRAME" || content == "<CIRCLE><FRAME>" ||
        std::regex_match(content, patterned);
}

std::vector<unsigned char> shape_pixels(const std::string& content, int width, int height) {
    if (width < 1 || height < 1 || width > 8192 || height > 8192 || static_cast<std::int64_t>(width) * height > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid built-in picture shape dimensions");
    if (!builtin_shape(content)) throw std::runtime_error("Unsupported built-in picture shape: " + content);
    const bool circle = content.compare(0, 8, "<CIRCLE>") == 0;
    const bool gradient = content.compare(0, 5, "<GRAD") == 0;
    const bool frame = content.find("FRAME") != std::string::npos;
    const bool triangle = content.compare(0, 5, "<TRI-") == 0;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 255);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto index = (static_cast<std::size_t>(y) * width + x) * 4;
        if (gradient) {
            const int extent = content[5] == 'X' ? width : height;
            const int step = content[5] == 'X' ? x : y;
            for (int channel = 0; channel < 3; ++channel) {
                const int start = (content[7 + channel] - '0') * 255 / 9;
                const int end = (content[11 + channel] - '0') * 255 / 9;
                pixels[index + channel] = extent == 1 ? start : start + (end - start) * step / (extent - 1);
            }
        } else if (triangle) {
            const double nx = (x + 0.5) / width, ny = (y + 0.5) / height;
            double progress = ny, across = nx, along_size = height, across_size = width;
            if (content.compare(5, 4, "DOWN") == 0) progress = 1 - ny;
            else if (content.compare(5, 4, "LEFT") == 0 || content.compare(5, 5, "RIGHT") == 0) {
                progress = content[5] == 'L' ? nx : 1 - nx; across = ny; along_size = width; across_size = height;
            }
            const double edge = progress - std::abs(across - 0.5) * 2;
            const bool inside = edge >= 0;
            const bool border = (1 - progress) * along_size < 1 || edge / std::sqrt(1 / (along_size * along_size) + 4 / (across_size * across_size)) < 1;
            pixels[index + 3] = inside && (!frame || border) ? 255 : 0;
        } else if (circle) {
            const double dx = x + 0.5 - width / 2.0, dy = y + 0.5 - height / 2.0;
            const double rx = width / 2.0, ry = height / 2.0;
            const bool outer = dx * dx / (rx * rx) + dy * dy / (ry * ry) <= 1;
            const bool inner = rx > 1 && ry > 1 && dx * dx / ((rx - 1) * (rx - 1)) + dy * dy / ((ry - 1) * (ry - 1)) < 1;
            pixels[index + 3] = outer && (!frame || !inner) ? 255 : 0;
        } else if (frame) pixels[index + 3] = x == 0 || x == width - 1 || y == 0 || y == height - 1 ? 255 : 0;
    }
    return pixels;
}
}
