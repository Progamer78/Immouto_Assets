#include "resources.hpp"
#include "images.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <limits>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb/stb_image.h>

namespace wolf {
std::vector<unsigned char> adjust_image_color(const std::vector<unsigned char>& rgba, const std::array<double, 3>& percent) {
    if (rgba.size() % 4) throw std::runtime_error("Invalid RGBA image buffer");
    for (double channel : percent) if (!std::isfinite(channel)) throw std::runtime_error("Invalid image color factor");
    auto result = rgba;
    for (std::size_t i = 0; i < result.size(); i += 4) for (std::size_t channel = 0; channel < 3; ++channel)
        result[i + channel] = static_cast<unsigned char>(std::clamp(rgba[i + channel] * percent[channel] / 100.0, 0.0, 255.0));
    return result;
}

std::pair<int, int> Resources::image_size(const std::string& relative) {
    const auto file = path(relative);
    const auto cached = image_sizes_.find(file);
    if (cached != image_sizes_.end()) return cached->second;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw std::runtime_error("Missing image resource: " + relative);
    if (std::filesystem::file_size(file) > std::numeric_limits<int>::max()) throw std::runtime_error("Image resource is too large");
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels))
        throw std::runtime_error("Cannot read image dimensions: " + relative);
    const auto dimensions = std::make_pair(width, height);
    image_sizes_.emplace(file, dimensions); return dimensions;
}
}
