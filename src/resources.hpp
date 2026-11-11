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
    bool exists(const std::string& relative, bool cache_writable = false);
    std::string read_text(const std::string& relative);
    std::pair<int, int> image_size(const std::string& relative);
    void invalidate() { directories_.clear(); image_sizes_.clear(); existence_.clear(); text_.clear(); text_bytes_ = 0; }
private:
    std::filesystem::path root_, writable_root_;
    std::map<std::filesystem::path, std::map<std::string, std::filesystem::path>> directories_;
    std::map<std::string, std::pair<int, int>> image_sizes_;
    std::map<std::string, bool> existence_;
    std::map<std::string, std::string> text_;
    std::size_t text_bytes_ = 0;
};
}
