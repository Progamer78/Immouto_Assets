#include "runtime.hpp"
#include "input_replay.hpp"
#include <iostream>
#include <filesystem>
#include <cstdlib>

int main(int argc, char** argv) {
    try {
        if (argc < 2) { std::cerr << "Usage: wolf-headless game.json [frames=120] [common-event-id=-1] [input.json] [state-output.json] [state-input.json]\n"; return 2; }
        const auto storage = std::getenv("WOLF_STORAGE_DIR");
        const auto resource_storage = std::getenv("WOLF_RESOURCE_STORAGE_DIR");
        wolf::Runtime runtime(wolf::load_json(argv[1]), storage ? storage : "", std::filesystem::u8path(argv[1]).parent_path().u8string(), resource_storage ? resource_storage : "");
        if (argc > 6) runtime.restore(wolf::load_json(argv[6]));
        if (argc > 3 && std::stoi(argv[3]) >= 0) runtime.start_common(std::stoi(argv[3]));
        const int frames = argc > 2 ? std::stoi(argv[2]) : 120;
        wolf::InputReplay replay(argc > 4 ? wolf::load_json(argv[4]) : wolf::Json::array());
        if (runtime.frame() > 0) (void)replay.input(runtime.frame() - 1);
        for (int i = 0; i < frames && runtime.error().empty(); ++i) runtime.tick(replay.input(runtime.frame()));
        if (argc > 5 && runtime.error().empty()) wolf::atomic_save(argv[5], runtime.snapshot());
        std::cout << runtime.diagnostics().dump(2) << '\n';
        return runtime.error().empty() ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
