#include "resources.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace wolf {
namespace {
std::string folded(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}
}
Resources::Resources(std::string root) : root_(std::filesystem::u8path(root)) {}

std::filesystem::path Resources::path(std::string relative) {
    if (root_.empty()) throw std::runtime_error("Game resource directory is not configured");
    std::replace(relative.begin(), relative.end(), '\\', '/');
    const auto name = std::filesystem::u8path(relative);
    if (name.is_absolute() || relative.find(':') != std::string::npos)
        throw std::runtime_error("Absolute game resource path is not allowed");
    auto result = root_;
    for (const auto& part : name) {
        if (part == ".." || part == ".") throw std::runtime_error("Game resource path escapes its directory");
        if (part.empty()) continue;
        const auto exact = result / part;
        if (std::filesystem::is_symlink(exact)) throw std::runtime_error("Symlink game resources are not supported");
        if (std::filesystem::exists(exact)) { result = exact; continue; }
        auto found = directories_.find(result);
        if (found == directories_.end()) {
            std::map<std::string, std::filesystem::path> entries;
            if (std::filesystem::is_directory(result))
                for (const auto& entry : std::filesystem::directory_iterator(result))
                    entries.emplace(folded(entry.path().filename().u8string()), entry.path().filename());
            found = directories_.emplace(result, std::move(entries)).first;
        }
        const auto entry = found->second.find(folded(part.u8string()));
        result /= entry == found->second.end() ? part : entry->second;
        if (std::filesystem::is_symlink(result)) throw std::runtime_error("Symlink game resources are not supported");
    }
    return result;
}

std::string Resources::read_text(const std::string& relative) {
    const auto file = path(relative);
    if (!std::filesystem::is_regular_file(file)) return "<NoFile>";
    if (std::filesystem::file_size(file) > 16 * 1024 * 1024) throw std::runtime_error("Game text resource is too large");
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read game text resource: " + relative);
    std::string result{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (result.compare(0, 3, "\xef\xbb\xbf") == 0) result.erase(0, 3);
    if (result.find('\0') != std::string::npos) throw std::runtime_error("Binary data used as a text resource: " + relative);
    return result;
}
}
