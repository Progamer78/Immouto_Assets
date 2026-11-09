#include "runtime.hpp"
#include <filesystem>
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace wolf {
void atomic_save(const std::string& filename, const Json& state) {
    const auto path = std::filesystem::u8path(filename);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
#ifdef _WIN32
    FILE* file = _wfopen(temporary.c_str(), L"wb");
#else
    FILE* file = std::fopen(temporary.c_str(), "wb");
#endif
    if (!file) throw std::runtime_error("Cannot create save temporary file");
    const auto bytes = state.dump();
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    ok = std::fflush(file) == 0 && ok;
#ifdef _WIN32
    ok = _commit(_fileno(file)) == 0 && ok;
#else
    ok = fsync(fileno(file)) == 0 && ok;
#endif
    ok = std::fclose(file) == 0 && ok;
    if (!ok) throw std::runtime_error("Save write failed; previous save was retained");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot atomically replace the save file");
#else
    std::filesystem::rename(temporary, path);
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    int directory = open(parent.c_str(), O_RDONLY);
    if (directory >= 0) { fsync(directory); close(directory); }
#endif
}
}
