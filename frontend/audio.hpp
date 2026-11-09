#pragma once
#include "runtime.hpp"
#include <filesystem>
#include <memory>

class Audio {
public:
    explicit Audio(const std::filesystem::path& root);
    ~Audio();
    void process(const wolf::Json& events, const std::array<int, 3>& volumes);
    void pause(bool paused);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
