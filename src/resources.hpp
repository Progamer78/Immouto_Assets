#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace wolf {
// WOLF filenames follow Windows case-insensitive lookup even on iOS.
class Resources {
public:
    explicit Resources(std::string root);
    std::filesystem::path path(std::string relative);
    std::string read_text(const std::string& relative);
private:
    std::filesystem::path root_;
    std::map<std::filesystem::path, std::map<std::string, std::filesystem::path>> directories_;
};
}
