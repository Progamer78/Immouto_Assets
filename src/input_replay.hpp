#pragma once
#include "runtime.hpp"

namespace wolf {
// Development input recording: each row supplies the complete held state
// starting at its frame. Mouse edges and wheel impulses last one tick.
class InputReplay {
public:
    explicit InputReplay(const Json& rows);
    Input input(std::uint64_t frame);
private:
    std::vector<std::pair<std::uint64_t, Input>> changes_;
    std::size_t next_ = 0;
    Input held_;
};
}
