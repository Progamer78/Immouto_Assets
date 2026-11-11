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
bool cacheable_data(std::string relative) {
    std::replace(relative.begin(), relative.end(), '\\', '/');
    while (relative.compare(0, 2, "./") == 0) relative.erase(0, 2);
    if (folded(relative).compare(0, 5, "data/") != 0) return false;
    for (const auto& part : std::filesystem::u8path(relative)) if (part == "..") return false;
    return true;
}
}
Resources::Resources(std::string root, std::string writable_root) : root_(std::filesystem::u8path(root)), writable_root_(std::filesystem::u8path(writable_root)) {}

std::filesystem::path Resources::path(std::string relative) {
    if (root_.empty()) throw std::runtime_error("Game resource directory is not configured");
    std::replace(relative.begin(), relative.end(), '\\', '/');
    while (relative.compare(0, 2, "./") == 0) relative.erase(0, 2);
    if (!writable_root_.empty()) {
        const auto lower = folded(relative);
        Resources writable(writable_root_.u8string());
        if (lower.compare(0, 13, "data/../save/") == 0) return writable.path(relative.substr(8));
        if (lower.compare(0, 5, "save/") == 0) return writable.path(relative);
        if (lower.compare(0, 5, "data/") == 0) {
            const auto overlay = writable.path(relative);
            if (std::filesystem::is_regular_file(overlay)) return overlay;
        }
    }
    const auto name = std::filesystem::u8path(relative);
    if (name.is_absolute() || relative.find(':') != std::string::npos)
        throw std::runtime_error("Absolute game resource path is not allowed");
    auto result = root_;
    for (const auto& part : name) {
        if (part == "..") throw std::runtime_error("Game resource path escapes its directory");
        if (part == ".") continue;
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

bool Resources::exists(const std::string& relative, bool cache_writable) {
    // Save paths remain live by default. The interpreter also caches its script
    // probes, invalidating after native writes and when returning to foreground.
    const bool cache = cache_writable || cacheable_data(relative);
    if (cache) {
        const auto found = existence_.find(relative);
        if (found != existence_.end()) return found->second;
    }
    const bool result = std::filesystem::exists(path(relative));
    if (cache) existence_[relative] = result;
    return result;
}

std::string Resources::read_text(const std::string& relative) {
    const bool cache = cacheable_data(relative);
    if (cache) {
        const auto found = text_.find(relative);
        if (found != text_.end()) return found->second;
    }
    const auto file = path(relative);
    if (!std::filesystem::is_regular_file(file)) return "<NoFile>";
    if (std::filesystem::file_size(file) > 16 * 1024 * 1024) throw std::runtime_error("Game text resource is too large");
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read game text resource: " + relative);
    std::string result{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (result.compare(0, 3, "\xef\xbb\xbf") == 0) result.erase(0, 3);
    if (result.find('\0') != std::string::npos) throw std::runtime_error("Binary data used as a text resource: " + relative);
    constexpr std::size_t limit = 4 * 1024 * 1024;
    if (cache && result.size() <= limit) {
        if (text_bytes_ + result.size() > limit) { text_.clear(); text_bytes_ = 0; }
        text_bytes_ += result.size(); text_[relative] = result;
    }
    return result;
}
}
