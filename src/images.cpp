#include "resources.hpp"
#include <fstream>
#include <limits>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb/stb_image.h>

namespace wolf {
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
