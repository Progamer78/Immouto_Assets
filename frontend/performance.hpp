#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Keep deadlines anchored to the clock, rather than adding a sleep after each
// vsynced frame. A missed deadline starts a new interval without a render burst.
class FramePacer {
    double interval_, next_ = 0;
public:
    explicit FramePacer(double fps) : interval_(1 / fps) {}
    void reset() { next_ = 0; }
    double remaining(double now) const { return std::max(0.0, next_ - now); }
    void begin(double now) {
        if (!next_ || now - next_ > interval_) next_ = now;
        next_ += interval_;
    }
};

class FrameMetrics {
    struct Sample { double frame, work, game; };
    std::array<Sample, 120> samples_{};
    std::size_t size_ = 0, next_ = 0;
public:
    struct Reading { double fps = 0, frame_ms = 0, work_ms = 0, game_ms = 0, peak_ms = 0; int slow = 0; };
    void reset() { size_ = next_ = 0; }
    void add(double frame, double work, double game) {
        if (!std::isfinite(frame) || frame <= 0) return;
        samples_[next_] = {frame, work, game};
        next_ = (next_ + 1) % samples_.size(); size_ = std::min(size_ + 1, samples_.size());
    }
    Reading read(double target_fps) const {
        Reading r;
        if (!size_) return r;
        for (std::size_t i = 0; i < size_; ++i) {
            const auto& s = samples_[i];
            r.frame_ms += s.frame; r.work_ms += s.work; r.game_ms += s.game;
            r.peak_ms = std::max(r.peak_ms, s.frame * 1000);
            r.slow += s.frame > 1.5 / target_fps;
        }
        r.fps = size_ / r.frame_ms;
        r.frame_ms *= 1000.0 / size_; r.work_ms *= 1000.0 / size_; r.game_ms *= 1000.0 / size_;
        return r;
    }
};

// Call after presentation: never destroy a texture while queued draws use it.
// The visible working set may exceed the budget without being repeatedly evicted.
template<class Cache>
void trim_texture_cache(Cache& cache, std::size_t limit, std::uint64_t generation) {
    std::size_t bytes = 0;
    for (const auto& item : cache) if (item.second) bytes += item.second->bytes();
    while (bytes > limit) {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (!it->second || it->second->last_used >= generation) continue;
            if (oldest == cache.end() || it->second->last_used < oldest->second->last_used) oldest = it;
        }
        if (oldest == cache.end()) break;
        bytes -= oldest->second->bytes(); cache.erase(oldest);
    }
}
