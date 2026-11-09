#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace wolf {
// WOLF filenames follow Windows case-insensitive lookup even on iOS.
class Resources {
public:
    explicit Resources(std::string root, std::string writable_root = {});
    std::filesystem::path path(std::string relative);
    std::string read_text(const std::string& relative);
    std::pair<int, int> image_size(const std::string& relative);
    void invalidate() { directories_.clear(); image_sizes_.clear(); }
private:
    std::filesystem::path root_, writable_root_;
    std::map<std::filesystem::path, std::map<std::string, std::filesystem::path>> directories_;
    std::map<std::filesystem::path, std::pair<int, int>> image_sizes_;
};
}
