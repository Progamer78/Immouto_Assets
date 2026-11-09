#include "asset_cache.hpp"
#include "runtime.hpp"
#include <stdexcept>

namespace wolf {
namespace fs = std::filesystem;
namespace {
bool valid_identity(const std::string& value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
fs::path relative_asset(const std::string& value) {
    const auto path = fs::u8path(value);
    if (path.is_absolute() || value.find(':') != std::string::npos || value.find('\\') != std::string::npos)
        throw std::runtime_error("Invalid asset manifest path");
    for (const auto& part : path) if (part == "..") throw std::runtime_error("Asset path escapes package");
    return path;
}
}

fs::path resolve_cached_game(const fs::path& bundle, const fs::path& storage,
        const std::function<void(std::size_t, std::size_t)>& progress) {
    const auto pointer = storage / "installed-game.json";
    if (fs::exists(bundle / "game.json")) {
        const auto manifest = load_json((bundle / "asset-manifest.json").u8string());
        if (manifest.at("format") != "wolf-ios-assets" || manifest.at("version") != 1)
            throw std::runtime_error("Incompatible asset manifest");
        const std::string identity = manifest.at("id");
        if (!valid_identity(identity)) throw std::runtime_error("Invalid asset package identity");
        const auto destination = storage / "game-cache" / identity;
        if (!fs::exists(destination / ".ready.json")) {
            const auto& files = manifest.at("files");
            std::size_t completed = 0;
            for (const auto& file : files) {
                const auto relative = relative_asset(file.at("path"));
                const auto source = bundle / relative, target = destination / relative;
                if (!fs::is_regular_file(source) || fs::is_symlink(source) ||
                    fs::file_size(source) != file.at("bytes").get<std::uintmax_t>())
                    throw std::runtime_error("Incomplete bundled asset: " + relative.u8string());
                fs::create_directories(target.parent_path());
                fs::copy_file(source, target, fs::copy_options::overwrite_existing);
                if (progress) progress(++completed, files.size());
            }
            if (!fs::exists(destination / "game.json")) throw std::runtime_error("Asset package has no runtime data");
            atomic_save((destination / ".ready.json").u8string(), {{"id", identity}});
        }
        atomic_save(pointer.u8string(), {{"id", identity}});
    }
    if (!fs::exists(pointer))
        throw std::runtime_error("Game assets are not installed. Install a full game build once, then update with development builds using the same app identifier.");
    const std::string identity = load_json(pointer.u8string()).at("id");
    if (!valid_identity(identity)) throw std::runtime_error("Invalid installed game identity");
    const auto game = storage / "game-cache" / identity;
    if (!fs::exists(game / ".ready.json") || !fs::exists(game / "game.json") ||
        load_json((game / ".ready.json").u8string()).at("id") != identity)
        throw std::runtime_error("Installed game assets are incomplete; reinstall a full game build");
    return game;
}
}
