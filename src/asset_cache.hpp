#pragma once
#include <filesystem>
#include <functional>

namespace wolf {
// Full builds install a versioned private copy once. Thin development updates reuse it.
std::filesystem::path resolve_cached_game(const std::filesystem::path& bundled_game,
    const std::filesystem::path& storage,
    const std::function<void(std::size_t, std::size_t)>& progress = {});
}
