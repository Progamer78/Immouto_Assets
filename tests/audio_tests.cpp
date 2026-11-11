#include "audio.hpp"
#include <SDL.h>
#include <cmath>
#include <fstream>
#include <iostream>

// Exercise the production mixer and SDL callback, recording real PCM output.
// Generated tones avoid depending on any private game assets in CI.
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("wolf-audio-" + std::to_string(SDL_GetPerformanceCounter()));
    try {
        std::filesystem::create_directories(root / "Data");
        {
            std::ofstream wave(root / "Data/tone.wav", std::ios::binary);
            auto le = [&](unsigned value, int bytes) { for (int i = 0; i < bytes; ++i) wave.put(static_cast<char>(value >> (8 * i))); };
            wave.write("RIFF", 4); le(36 + 48000 * 2, 4); wave.write("WAVEfmt ", 8); le(16, 4);
            le(1, 2); le(1, 2); le(48000, 4); le(96000, 4); le(2, 2); le(16, 2);
            wave.write("data", 4); le(48000 * 2, 4);
            for (int i = 0; i < 48000; ++i) le(static_cast<std::uint16_t>(static_cast<std::int16_t>(12000 * std::sin(i * .1))), 2);
        }
        const auto output = root / "output.raw";
        SDL_SetMainReady();
        SDL_setenv("SDL_AUDIODRIVER", "disk", 1);
        SDL_setenv("SDL_DISKAUDIOFILE", output.u8string().c_str(), 1);
        auto event = [](const char* operation, int channel) {
            return wolf::Json{{"operation", operation}, {"kind", 2}, {"file", "tone.wav"}, {"channel", channel},
                {"fade_ms", 0}, {"volume", 60}, {"tempo", 100}, {"start_ms", 0}, {"loop_ms", 0}};
        };
        {
            Audio audio(root);
            audio.process(wolf::Json::array({event("preload", 0), event("play", 0)}), {100, 100, 100});
            SDL_Delay(80);
            audio.process(wolf::Json::array({event("play", 1)}), {100, 100, 100});
            SDL_Delay(80);
            audio.process(wolf::Json::array({event("stop", 0)}), {100, 100, 100});
            SDL_Delay(40);
            audio.process(wolf::Json::array({event("stop", -1), event("release_unused", 0), event("play", 0)}), {100, 100, 100});
            audio.pause(true); audio.pause(true); audio.pause(false); audio.pause(false);
            SDL_Delay(80);
            audio.process(wolf::Json::array({event("stop", -1)}), {100, 100, 100});
            SDL_Delay(80);
        }
        SDL_Quit();
        std::ifstream stream(output, std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("Audio callback output file is missing");
        const auto count = static_cast<std::size_t>(stream.tellg()) / sizeof(float);
        if (count < 4096) throw std::runtime_error("Audio callback produced too few samples");
        std::vector<float> pcm(count); stream.seekg(0);
        stream.read(reinterpret_cast<char*>(pcm.data()), static_cast<std::streamsize>(count * sizeof(float)));
        stream.close();
        std::size_t audible = 0;
        for (float sample : pcm) {
            if (!std::isfinite(sample) || std::abs(sample) > 1.01f) throw std::runtime_error("Invalid mixer sample");
            audible += std::abs(sample) > .01f;
        }
        if (audible < 2048) throw std::runtime_error("Async/reused sound effects produced silence");
        for (std::size_t i = count - 2048; i < count; ++i)
            if (std::abs(pcm[i]) > .0001f) throw std::runtime_error("Stopped effects must become silent");
        std::cout << "Async sound preload, shared playback, channel stop, release, pause/resume and PCM output passed\n";
        std::filesystem::remove(output); std::filesystem::remove(root / "Data/tone.wav");
        std::filesystem::remove(root / "Data"); std::filesystem::remove(root);
        return 0;
    } catch (const std::exception& error) {
        SDL_Quit(); std::cerr << error.what() << '\n'; return 1;
    }
}
