#include "runtime.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc < 2) { std::cerr << "Usage: wolf-headless game.json [frames=120] [common-event-id]\n"; return 2; }
        wolf::Runtime runtime(wolf::load_json(argv[1]));
        if (argc > 3) runtime.start_common(std::stoi(argv[3]));
        const int frames = argc > 2 ? std::stoi(argv[2]) : 120;
        for (int i = 0; i < frames && runtime.error().empty(); ++i) runtime.tick();
        std::cout << runtime.diagnostics().dump(2) << '\n';
        return runtime.error().empty() ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
