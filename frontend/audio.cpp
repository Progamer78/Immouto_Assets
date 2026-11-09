#include "audio.hpp"
#include <SDL.h>
#include <miniaudio/miniaudio.h>
#include <algorithm>
#include <fstream>
#include <regex>

struct Audio::Impl {
    struct Voice {
        ma_sound sound{};
        std::string file;
        int channel, kind;
        float volume = 1;
        bool retiring = false;
        explicit Voice(ma_engine* engine, const std::filesystem::path& path, int channel_id, int sound_kind) : file(path.u8string()), channel(channel_id), kind(sound_kind) {
            const auto flags = MA_SOUND_FLAG_NO_SPATIALIZATION | (kind == 2 ? MA_SOUND_FLAG_DECODE : MA_SOUND_FLAG_STREAM);
#ifdef _WIN32
            const auto result = ma_sound_init_from_file_w(engine, path.c_str(), flags, nullptr, nullptr, &sound);
#else
            const auto result = ma_sound_init_from_file(engine, file.c_str(), flags, nullptr, nullptr, &sound);
#endif
            if (result != MA_SUCCESS) throw std::runtime_error("Cannot decode game audio: " + file + " (" + std::to_string(result) + ")");
        }
        ~Voice() { ma_sound_uninit(&sound); }
    };
    wolf::Resources resources;
    ma_engine engine{};
    SDL_AudioDeviceID device = 0;
    std::vector<std::unique_ptr<Voice>> voices;
    static void callback(void* data, Uint8* output, int bytes) {
        auto& self = *static_cast<Impl*>(data);
        if (ma_engine_read_pcm_frames(&self.engine, output, bytes / (sizeof(float) * 2), nullptr) != MA_SUCCESS)
            SDL_memset(output, 0, bytes);
    }
    explicit Impl(const std::filesystem::path& root) : resources(root.u8string()) {
        auto config = ma_engine_config_init(); config.noDevice = MA_TRUE; config.channels = 2; config.sampleRate = 48000;
        if (ma_engine_init(&config, &engine) != MA_SUCCESS) throw std::runtime_error("Cannot initialize native audio mixer");
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            SDL_AudioSpec desired{}; desired.freq = 48000; desired.format = AUDIO_F32SYS; desired.channels = 2;
            desired.samples = 512; desired.callback = callback; desired.userdata = this;
            device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        }
        if (!device) { ma_engine_uninit(&engine); throw std::runtime_error(SDL_GetError()); }
        SDL_PauseAudioDevice(device, 0);
    }
    ~Impl() { SDL_CloseAudioDevice(device); voices.clear(); ma_engine_uninit(&engine); }
};

Audio::Audio(const std::filesystem::path& root) : impl_(std::make_unique<Impl>(root)) {}
Audio::~Audio() = default;
void Audio::pause(bool paused) { SDL_PauseAudioDevice(impl_->device, paused ? 1 : 0); }
void Audio::process(const wolf::Json& events, const std::array<int, 3>& volumes) {
    auto& voices = impl_->voices;
    voices.erase(std::remove_if(voices.begin(), voices.end(), [](const auto& voice) {
        return ma_sound_at_end(&voice->sound) || (voice->retiring && !ma_sound_is_playing(&voice->sound));
    }), voices.end());
    for (auto& voice : voices) ma_sound_set_volume(&voice->sound, voice->volume * std::max(0, volumes.at(voice->kind)) / 100.f);
    for (const auto& event : events) {
        const int kind = event.at("kind"), fade_ms = event.at("fade_ms");
        if (kind < 0 || kind > 2 || event.at("operation") != "play") throw std::runtime_error("Unsupported native audio event");
        const auto path = impl_->resources.path("Data/" + event.at("file").get<std::string>());
        const int channel = event.at("channel");
        if (kind != 2) {
            const auto same = std::find_if(voices.begin(), voices.end(), [&](const auto& voice) {
                return !voice->retiring && voice->kind == kind && voice->file == path.u8string();
            });
            if (same != voices.end()) {
                (*same)->volume = event.at("volume").get<float>() / 100.f;
                ma_sound_set_volume(&(*same)->sound, (*same)->volume * std::max(0, volumes.at(kind)) / 100.f);
                ma_sound_set_pitch(&(*same)->sound, event.at("tempo").get<float>() / 100.f);
                continue;
            }
        }
        voices.erase(std::remove_if(voices.begin(), voices.end(), [&](const auto& voice) {
            if (voice->kind != kind || (kind == 2 && (voice->file != path.u8string() || voice->channel != channel))) return false;
            if (fade_ms == 0) return true;
            voice->retiring = true;
            ma_sound_set_fade_in_milliseconds(&voice->sound, -1, 0, fade_ms);
            ma_sound_set_stop_time_in_milliseconds(&voice->sound, ma_engine_get_time_in_milliseconds(&impl_->engine) + fade_ms);
            return false;
        }), voices.end());
        auto voice = std::make_unique<Impl::Voice>(&impl_->engine, path, channel, kind);
        voice->volume = event.at("volume").get<float>() / 100.f;
        ma_sound_set_volume(&voice->sound, voice->volume * std::max(0, volumes.at(kind)) / 100.f);
        ma_sound_set_pitch(&voice->sound, event.at("tempo").get<float>() / 100.f);
        if (kind != 2) {
            ma_uint32 sample_rate = 0;
            if (ma_sound_get_data_format(&voice->sound, nullptr, nullptr, &sample_rate, nullptr, 0) != MA_SUCCESS)
                throw std::runtime_error("Cannot read music sample rate");
            const int loop_ms = event.at("loop_ms");
            if (loop_ms < 0) throw std::runtime_error("Unsupported negative audio loop position");
            ma_uint64 loop_start = static_cast<ma_uint64>(loop_ms) * sample_rate / 1000, loop_end = MA_UINT64_MAX;
            if (loop_ms == 0) {
                std::ifstream stream(path, std::ios::binary); std::string header(1024, '\0');
                stream.read(header.data(), header.size()); header.resize(static_cast<std::size_t>(stream.gcount()));
                std::smatch match;
                if (std::regex_search(header, match, std::regex("LOOPSTART=([0-9]+)"))) {
                    loop_start = std::stoull(match[1]);
                    if (std::regex_search(header, match, std::regex("LOOPLENGTH=([0-9]+)"))) loop_end = loop_start + std::stoull(match[1]);
                }
            }
            if (ma_data_source_set_loop_point_in_pcm_frames(ma_sound_get_data_source(&voice->sound), loop_start, loop_end) != MA_SUCCESS ||
                ma_sound_seek_to_pcm_frame(&voice->sound, event.at("start_ms").get<ma_uint64>() * sample_rate / 1000) != MA_SUCCESS)
                throw std::runtime_error("Cannot set music start/loop position");
            ma_sound_set_looping(&voice->sound, MA_TRUE);
            if (fade_ms) ma_sound_set_fade_in_milliseconds(&voice->sound, 0, 1, fade_ms);
        }
        if (ma_sound_start(&voice->sound) != MA_SUCCESS) throw std::runtime_error("Cannot start native sound playback");
        voices.push_back(std::move(voice));
    }
}
