#include "runtime.hpp"
#include <algorithm>

namespace wolf {
namespace {
Json character(int x, int y) {
    return {{"x", x}, {"y", y}, {"direction", 2}, {"speed", 3}, {"frequency", 3},
        {"animation_speed", 3}, {"graphic_id", -1}, {"opacity", 255}, {"height", 0},
        {"idle_animation", false}, {"move_animation", true}, {"fixed_direction", false},
        {"through", false}, {"on_top", false}, {"half_tile", true}, {"pattern", 1}};
}
}

std::string Runtime::character_key(int target, Context context) {
    if (target == -2) {
        if (!characters_.contains("hero")) characters_["hero"] = character(hero_x_, hero_y_);
        return "hero";
    }
    if (target == -1) {
        if (context.event < 0) fail("Character target 'this event' has no map-event context");
        target = context.event;
    }
    if (target <= -3 && target >= -7) {
        const std::string key = "party:" + std::to_string(-target - 2);
        if (!characters_.contains(key)) characters_[key] = character(hero_x_, hero_y_);
        return key;
    }
    if (target < 0) fail("Invalid character target");
    const std::string key = std::to_string(map_) + ":" + std::to_string(target);
    if (characters_.contains(key)) return key;
    for (const auto& event : current_map()->at("events")) {
        if (event.at("id") != target) continue;
        auto state = character(event.at("x").get<int>() * 2, event.at("y").get<int>() * 2);
        const auto& pages = event.at("pages");
        for (int p = static_cast<int>(pages.size()) - 1; p >= 0; --p) {
            const auto& page = pages.at(p);
            if (!page_active(page, {-1, target, p, map_})) continue;
            const auto& flags = page.at("route").at("flags");
            state["animation_speed"] = flags.at(0); state["speed"] = flags.at(1);
            state["frequency"] = flags.at(2);
            const int options = flags.at(4);
            state["idle_animation"] = (options & 1) != 0;
            state["move_animation"] = (options & 2) != 0;
            state["fixed_direction"] = (options & 4) != 0;
            state["through"] = (options & 8) != 0;
            state["on_top"] = (options & 16) != 0;
            state["image"] = page.at("image"); state["chip"] = page.at("chip");
            break;
        }
        characters_[key] = std::move(state);
        return key;
    }
    fail("Character route target event is missing: " + std::to_string(target));
    return {};
}

void Runtime::party(const Json& cmd, Context context) {
    const int options = cmd.at("args").at(0), operation = options & 15;
    if (options & ~0x1ff) fail("Unsupported party graphics flags");
    auto& images = party_.at("images");
    if (operation == 4) {
        if (cmd.at("args").size() != 1 || (options & 0x100)) fail("Unexpected party special-operation layout");
        switch ((options >> 4) & 15) {
        case 1: for (auto& image : images) image = ""; return;
        case 5: party_["visible"] = false; return;
        case 6: party_["visible"] = true; return;
        default: fail("Unsupported party special operation " + std::to_string((options >> 4) & 15));
        }
    }
    if (operation > 2 || (options & 0xf0)) fail("Unsupported party graphics operation");
    const bool variable = (options & 0x100) != 0;
    if (cmd.at("args").size() != (variable ? 3 : 2) || (operation == 0 && variable))
        fail("Unexpected party graphics argument layout");
    const int slot = number(cmd.at("args").at(1), context) - 1;
    if (slot < 0 || slot >= 6) fail("Party member index is outside 1..6");
    if (operation == 0 || operation == 1) {
        for (int i = operation == 0 ? slot : 5; operation == 0 ? i < 5 : i > slot; operation == 0 ? ++i : --i)
            images[i] = images[i + (operation == 0 ? 1 : -1)];
        if (operation == 0) { images[5] = ""; return; }
    }
    if (variable) {
        int ref = cmd.at("args").at(2);
        if (ref < 3000000) ref = 3000000 + number(ref, context);
        images[slot] = text(ref, context);
    } else images[slot] = expand(cmd.at("strings").at(0), context);
}

bool Runtime::route_pending(const Run& run) const {
    return std::any_of(routes_.begin(), routes_.end(), [&](const Route& route) {
        return (run.waiting_all_routes || route.id == run.waiting_route) &&
            (route.pc < route.source.at("route").at("commands").size() || route.wake > frame_);
    });
}

void Runtime::start_route(Run& run, const Json& cmd) {
    if (cmd.at("args").size() != 1) fail("Unexpected character-route argument layout");
    const auto& flags = cmd.at("route").at("flags");
    // The five prefix bytes are retained by the converter. All explicit routes in
    // this title have this prefix; only the final byte contains route options.
    if (flags.size() != 6 || Json::array({flags.at(0), flags.at(1), flags.at(2), flags.at(3), flags.at(4)}) != Json::array({3, 3, 3, 0, 3}))
        fail("Unsupported character-route header");
    const int options = flags.at(5);
    if (options & ~6) fail("Repeating/unknown character-route options are not implemented");
    Route route; route.id = next_route_++; route.source = cmd;
    route.context = run.stack.back().context;
    route.target = character_key(number(cmd.at("args").at(0), route.context), route.context);
    // A new route replaces an older route for the same character. A waiter on
    // that old route is released; it must not start waiting on the replacement.
    routes_.erase(std::remove_if(routes_.begin(), routes_.end(), [&](const Route& r) { return r.target == route.target; }), routes_.end());
    advance_route(route);
    if (options & 4) run.waiting_route = route.id;
    if (route.pc < cmd.at("route").at("commands").size() || route.wake > frame_) routes_.push_back(std::move(route));
}

void Runtime::advance_route(Route& route) {
    if (route.wake > frame_) return;
    const auto& commands = route.source.at("route").at("commands");
    auto& state = characters_.at(route.target);
    while (route.pc < commands.size()) {
        error_context_ = route.context; error_command_ = route.source;
        crash_breadcrumb.code.store(201, std::memory_order_relaxed);
        crash_breadcrumb.common.store(route.context.common, std::memory_order_relaxed);
        crash_breadcrumb.map.store(route.context.map, std::memory_order_relaxed);
        crash_breadcrumb.event.store(route.context.event, std::memory_order_relaxed);
        crash_breadcrumb.offset.store(route.source.at("offset"), std::memory_order_relaxed);
        const auto& cmd = commands.at(route.pc++);
        const int code = cmd.at("code");
        if (cmd.at("extra") != Json::array({0})) fail("Unsupported character-route extension");
        auto argument = [&](int i) { return number(cmd.at("args").at(i), route.context); };
        std::size_t expected = 0;
        if (code == 28 || code == 55) expected = 2;
        else if ((code >= 29 && code <= 31) || (code >= 44 && code <= 47) || code == 58) expected = 1;
        if (cmd.at("args").size() != expected) fail("Unsupported argument layout in route command " + std::to_string(code));
        if (code >= 8 && code <= 15) {
            static const int directions[] = {2, 4, 6, 8, 1, 3, 7, 9};
            state["direction"] = directions[code - 8];
        } else if (code >= 32 && code <= 41) {
            static const char* fields[] = {"idle_animation", "move_animation", "fixed_direction", "through", "on_top"};
            state[fields[(code - 32) / 2]] = (code % 2) == 0;
        } else switch (code) {
        case 28: set_number(cmd.at("args").at(0), argument(1), route.context); break;
        case 55: set_number(cmd.at("args").at(0), assign(number(cmd.at("args").at(0), route.context), argument(1), 1), route.context); break;
        case 29: case 30: case 31: {
            const int value = argument(0);
            if (value < 0 || value > 6) fail("Invalid character speed/frequency");
            state[code == 29 ? "speed" : code == 30 ? "frequency" : "animation_speed"] = value;
            break;
        }
        case 44: state["graphic_id"] = argument(0); break;
        case 45: state["opacity"] = std::clamp(argument(0), 0, 255); break;
        case 47: route.wake = frame_ + static_cast<std::uint64_t>(std::max(0, argument(0))); break;
        case 48: case 49: state["half_tile"] = code == 48; break;
        case 50: case 51: case 52: state["pattern"] = code - 49; break;
        case 56: case 57: state["pattern"] = code - 52; break;
        case 58: state["height"] = argument(0); break;
        default: fail("Unsupported character-route command " + std::to_string(code) + " at route index " + std::to_string(route.pc - 1));
        }
        if (route.wake > frame_) return;
    }
}
}
