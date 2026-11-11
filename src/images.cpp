#include "resources.hpp"
#include "images.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <limits>
#include <vector>
#include <cstdio>
#include <memory>
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
    // Hover/dimension queries run many times per tick. Consult this cache
    // before case-insensitive path resolution and writable-overlay probing.
    const auto cached = image_sizes_.find(relative);
    if (cached != image_sizes_.end()) return cached->second;
    const auto file = path(relative);
#ifdef _WIN32
    auto* handle = _wfopen(file.c_str(), L"rb");
#else
    auto* handle = std::fopen(file.c_str(), "rb");
#endif
    std::unique_ptr<FILE, decltype(&std::fclose)> stream(handle, std::fclose);
    if (!stream) throw std::runtime_error("Missing image resource: " + relative);
    if (std::filesystem::file_size(file) > std::numeric_limits<int>::max()) throw std::runtime_error("Image resource is too large");
    int width = 0, height = 0, channels = 0;
    // Read headers rather than copying the entire PNG/JPEG just for its size.
    if (!stbi_info_from_file(stream.get(), &width, &height, &channels))
        throw std::runtime_error("Cannot read image dimensions: " + relative);
    const auto dimensions = std::make_pair(width, height);
    image_sizes_.emplace(relative, dimensions); return dimensions;
}
}
