#include "runtime.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace wolf {
CrashBreadcrumb crash_breadcrumb;
namespace {
int arg(const Json& c, std::size_t i) { return c.at("args").at(i).get<int>(); }
std::int32_t wrap(std::int64_t value) {
    const auto unsigned_value = static_cast<std::uint32_t>(value);
    return unsigned_value <= 0x7fffffffU ? static_cast<std::int32_t>(unsigned_value)
        : static_cast<std::int32_t>(static_cast<std::int64_t>(unsigned_value) - 0x100000000LL);
}
}

Json load_json(const std::string& filename) {
    std::ifstream stream(std::filesystem::u8path(filename), std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open " + filename);
    return Json::parse(stream);
}

Runtime::Runtime(Json project, std::string save_root) : project_(std::move(project)), save_root_(std::move(save_root)) {
    if (project_.at("format") != "wolf-ios-ir" || project_.at("version") != 1)
        fail("Unsupported intermediate data version");
    std::uint64_t fingerprint = 14695981039346656037ULL;
    for (unsigned char byte : project_.dump()) { fingerprint ^= byte; fingerprint *= 1099511628211ULL; }
    std::ostringstream identity; identity << std::hex << fingerprint; project_id_ = identity.str();
    databases_ = project_.at("databases");
    const auto& start = databases_.at("system").at("types").at(7).at("rows").at(0);
    map_ = start.at(0); hero_x_ = start.at(1); hero_y_ = start.at(2);
    if (!current_map()) fail("Initial map is absent from the converted data");
    // Engine-owned values documented in WOLF's system-variable list.
    numbers_[9000115] = 319; // This compatibility target is the supplied 3.191 runtime.
    numbers_[9000116] = project_.at("game").at("width");
    numbers_[9000117] = project_.at("game").at("height");
    numbers_[9000118] = project_.at("game").value("tile_size", 16);
    numbers_[9000084] = project_.at("game").at("width") == 320 && project_.at("game").at("height") == 240 ? 0 :
        project_.at("game").at("width") == 640 && project_.at("game").at("height") == 480 ? 1 :
        project_.at("game").at("width") == 800 && project_.at("game").at("height") == 600 ? 2 : -1;
    for (int ref = 9000100; ref <= 9000102; ++ref) numbers_[ref] = 100;
}

void Runtime::fail(const std::string& message) const { throw std::runtime_error(message); }

const Json& Runtime::common(int id) const {
    const auto& events = project_.at("common").at("events");
    if (id >= 0 && static_cast<std::size_t>(id) < events.size() && events.at(id).at("id") == id) return events.at(id);
    for (const auto& event : events) if (event.at("id") == id) return event;
    fail("Missing common event " + std::to_string(id));
    return events;
}

const Json* Runtime::current_map() const {
    const auto& rows = databases_.at("system").at("types").at(0).at("rows");
    if (map_ < 0 || static_cast<std::size_t>(map_) >= rows.size()) return nullptr;
    const std::string path = rows.at(map_).at(0);
    for (const auto& candidate : project_.at("maps")) if (candidate.at("file") == path) return &candidate;
    return nullptr;
}

const Json& Runtime::commands(Context c) const {
    if (c.common >= 0) return common(c.common).at("commands");
    const auto& rows = databases_.at("system").at("types").at(0).at("rows");
    const std::string file = rows.at(c.map).at(0);
    for (const auto& m : project_.at("maps")) if (m.at("file") == file)
        for (const auto& e : m.at("events")) if (e.at("id") == c.event)
            return e.at("pages").at(c.page).at("commands");
    fail("Missing command list");
    return project_;
}

std::uint64_t Runtime::key(int ref, Context c) const {
    if (ref >= 1600000 && ref < 1600100) {
        if (c.common < 0) fail("Common-self reference outside a common event");
        ref = 15000000 + c.common * 100 + ref - 1600000;
    }
    if (ref >= 1100000 && ref < 1100010) {
        if (c.event < 0) fail("Map-self reference outside a map event");
        ref = 1000000 + c.event * 10 + ref - 1100000;
    }
    if (ref >= 1000000 && ref < 1100000)
        return (1ULL << 48) | (static_cast<std::uint64_t>(c.map) << 24) | static_cast<unsigned>(ref);
    if ((ref >= 2000000 && ref < 4000000) || (ref >= 9000000 && ref < 9100000) ||
        (ref >= 9900000 && ref < 10000000) || (ref >= 15000000 && ref < 1000000000))
        return static_cast<unsigned>(ref);
    fail("Unsupported variable reference " + std::to_string(ref));
    return 0;
}

Json& Runtime::cell(int ref) {
    const char* bank = nullptr; int offset = 0;
    if (ref >= 1000000000 && ref < 1100000000) { bank = "user"; offset = ref - 1000000000; }
    if (ref >= 1100000000 && ref < 1200000000) { bank = "mutable"; offset = ref - 1100000000; }
    if (ref >= 1300000000 && ref < 1400000000) { bank = "system"; offset = ref - 1300000000; }
    if (!bank) fail("Invalid database reference");
    return databases_.at(bank).at("types").at(offset / 1000000).at("rows")
        .at((offset % 1000000) / 100).at(offset % 100);
}

std::int32_t Runtime::number(int ref, Context c) {
    if (ref < 1000000) return ref;
    if (ref >= 1000000000) {
        auto& value = cell(ref);
        if (!value.is_number_integer()) fail("Numeric read of a string database field");
        return value.get<std::int32_t>();
    }
    if (ref >= 8000000 && ref < 9000000)
        return std::uniform_int_distribution<int>(0, ref - 8000000)(random_);
    if (ref == 9000071) return input_.mouse_x;
    if (ref == 9000072) return input_.mouse_y;
    const auto position = numbers_.find(key(ref, c));
    return position == numbers_.end() ? 0 : position->second;
}

void Runtime::set_number(int ref, std::int32_t value, Context c) {
    if (ref >= 1000000000) {
        auto& field = cell(ref);
        if (!field.is_number_integer()) fail("Numeric write to a string database field");
        field = value;
    } else numbers_[key(ref, c)] = value;
}

std::string Runtime::text(int ref, Context c) {
    if (ref >= 1000000000) {
        auto& value = cell(ref);
        if (!value.is_string()) fail("String read of a numeric database field");
        return value.get<std::string>();
    }
    auto position = strings_.find(key(ref, c));
    return position == strings_.end() ? "" : position->second;
}
void Runtime::set_text(int ref, std::string value, Context c) {
    if (ref >= 1000000000) {
        auto& field = cell(ref);
        if (!field.is_string()) fail("String write to a numeric database field");
        field = std::move(value);
    } else strings_[key(ref, c)] = std::move(value);
}

std::string Runtime::expand(std::string value, Context c) {
    // Expand variable substitutions; drawing controls remain for the text renderer.
    for (int pass = 0; pass < 16; ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] != '\\') continue;
            const auto bracket = value.find('[', i + 1);
            if (bracket == std::string::npos || bracket - i > 8) continue;
            std::string tag = value.substr(i + 1, bracket - i - 1);
            std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char ch) { return std::tolower(ch); });
            if (tag != "v" && tag != "s" && tag != "cself" && tag != "self" && tag != "sys") continue;
            const auto end = value.find(']', bracket);
            if (end == std::string::npos) fail("Unclosed variable substitution");
            const auto index_text = value.substr(bracket + 1, end - bracket - 1);
            if (index_text.empty() || index_text.find_first_not_of("0123456789") != std::string::npos) continue;
            const int index = std::stoi(index_text);
            int ref = tag == "v" ? 2000000 + index : tag == "s" ? 3000000 + index :
                tag == "sys" ? 9000000 + index : tag == "self" ? 1100000 + index : 1600000 + index;
            const bool is_string = tag == "s" || (tag == "cself" && index >= 5 && index <= 9);
            auto replacement = is_string ? text(ref, c) : std::to_string(number(ref, c));
            value.replace(i, end - i + 1, replacement);
            changed = true; break;
        }
        if (!changed) return value;
    }
    fail("Variable substitution recursion limit"); return {};
}

bool Runtime::compare(int a, int b, int operation) const {
    switch (operation) {
    case 0: return a > b; case 1: return a >= b; case 2: return a == b;
    case 3: return a <= b; case 4: return a < b; case 5: return a != b; case 6: return (a & b) != 0;
    default: fail("Unknown comparison operator"); return false;
    }
}

std::int32_t Runtime::assign(std::int32_t current, std::int64_t value, int op) const {
    switch (op) {
    case 0: break; case 1: value = current + value; break; case 2: value = current - value; break;
    case 3: value = current * value; break; case 4: value = value == 0 ? 0 : current / value; break;
    case 5: value = value == 0 ? 0 : current % value; break;
    case 6: value = std::max<std::int64_t>(current, value); break;
    case 7: value = std::min<std::int64_t>(current, value); break;
    case 8: value = std::abs(value); break;
    default: fail("Unsupported assignment operator " + std::to_string(op));
    }
    return wrap(value);
}

void Runtime::start_common(int id) {
    common(id);
    Run run; run.owner = "manual:" + std::to_string(id);
    Frame frame; frame.context = {id, -1, -1, map_}; run.stack.push_back(std::move(frame));
    runs_.push_back(std::move(run));
}

bool Runtime::page_active(const Json& page, Context c) {
    for (int i = 0; i < 4; ++i) {
        const int flag = page.at("switches").at(i);
        if ((flag & 15) == 1 && !compare(number(page.at("variables").at(i), c),
            number(page.at("values").at(i), c), flag >> 4)) return false;
    }
    return true;
}

void Runtime::schedule() {
    auto active = [&](const std::string& owner) {
        return std::any_of(runs_.begin(), runs_.end(), [&](const Run& run) { return run.owner == owner; });
    };
    // Order is explicit and deterministic. Exact WOLF scheduling still needs reference testing.
    for (const auto& event : project_.at("common").at("events")) {
        const int id = event.at("id"), mode = event.at("mode");
        if (mode == 0) continue;
        Context c{id, -1, -1, map_};
        const auto owner = "common:" + std::to_string(id);
        if (active(owner)) continue;
        if (mode != 3 && !compare(number(event.at("condition_variable"), c),
                number(event.at("condition_value"), c), event.at("condition_operator"))) continue;
        Run run; run.owner = owner; run.blocking = mode == 1;
        Frame frame; frame.context = c; run.stack.push_back(std::move(frame));
        runs_.push_back(std::move(run));
    }
    const auto* m = current_map();
    if (!m) fail("Current map is missing");
    for (const auto& event : m->at("events")) {
        const int id = event.at("id");
        const auto owner = "map:" + std::to_string(map_) + ":" + std::to_string(id);
        if (active(owner)) continue;
        const auto& pages = event.at("pages");
        for (int i = static_cast<int>(pages.size()) - 1; i >= 0; --i) {
            Context c{-1, id, i, map_};
            const auto& page = pages.at(i);
            if (!page_active(page, c)) continue;
            const int trigger = page.at("trigger");
            if (trigger == 1 || trigger == 2) {
                Run run; run.owner = owner; run.blocking = trigger == 1;
                Frame frame; frame.context = c; run.stack.push_back(std::move(frame));
                runs_.push_back(std::move(run));
            }
            break;
        }
    }
}

void Runtime::branch(Frame& f, int indent, const std::vector<bool>& conditions) {
    const auto& list = commands(f.context);
    for (std::size_t i = f.pc; i < list.size(); ++i) {
        const auto& c = list.at(i);
        if (c.at("indent") != indent) continue;
        const int code = c.at("code");
        if (code == 499) { f.pc = i + 1; return; }
        if (code == 420 || code == 421) { f.pc = i + 1; return; }
        if (code == 401) {
            const int index = arg(c, 0) - 1;
            if (index >= 0 && static_cast<std::size_t>(index) < conditions.size() && conditions.at(index)) {
                f.pc = i + 1; return;
            }
        }
    }
    fail("Conditional branch has no matching terminator");
}

void Runtime::tick(const Input& input) {
    if (!error_.empty() || quit_) return;
    input_ = input;
    try {
        for (auto& route : routes_) advance_route(route);
        routes_.erase(std::remove_if(routes_.begin(), routes_.end(), [&](const Route& r) {
            return r.pc >= r.source.at("route").at("commands").size() && r.wake <= frame_;
        }), routes_.end());
        schedule();
        // Documented execution priority: map events, automatic common events,
        // then parallel common events in ID order. Trigger re-evaluation and
        // cancellation rules still need more behavioral reference tests.
        auto priority = [&](const Run& run) {
            if (run.stack.empty()) return std::pair<int, int>{3, 0};
            const auto c = run.stack.front().context;
            return std::pair<int, int>{c.common < 0 ? 0 : common(c.common).at("mode") == 1 ? 1 : 2,
                c.common < 0 ? c.event : c.common};
        };
        std::stable_sort(runs_.begin(), runs_.end(), [&](const Run& a, const Run& b) { return priority(a) < priority(b); });
        for (auto& run : runs_) if (run.wake <= frame_ && !route_pending(run)) {
            run.waiting_route = 0; run.waiting_all_routes = false; advance(run);
        }
        runs_.erase(std::remove_if(runs_.begin(), runs_.end(), [](const Run& r) { return r.stack.empty(); }), runs_.end());
        ++frame_;
    } catch (const std::exception& error) { error_ = error.what(); }
    try { flush_saves(); } catch (const std::exception& error) { error_ = error.what(); }
}

void Runtime::advance(Run& run) {
    for (int budget = 0; budget < 20000 && !run.stack.empty(); ++budget) {
        auto& f = run.stack.back();
        const auto& list = commands(f.context);
        if (f.pc >= list.size()) {
            const auto completed = f;
            run.stack.pop_back();
            if (completed.return_target >= 0 && !run.stack.empty()) {
                const int ref = common(completed.context.common).at("return_id");
                set_number(completed.return_target, number(ref, completed.context), run.stack.back().context);
            }
            continue;
        }
        const auto& cmd = list.at(f.pc++);
        crash_breadcrumb.code.store(cmd.at("code"), std::memory_order_relaxed);
        crash_breadcrumb.common.store(f.context.common, std::memory_order_relaxed);
        crash_breadcrumb.map.store(f.context.map, std::memory_order_relaxed);
        crash_breadcrumb.event.store(f.context.event, std::memory_order_relaxed);
        crash_breadcrumb.offset.store(cmd.at("offset"), std::memory_order_relaxed);
        error_context_ = f.context; error_command_ = cmd;
        ++executed_[cmd.at("code").get<int>()];
        execute(run, cmd);
        if (run.wake > frame_ || route_pending(run)) return;
        if (!message_.empty()) return;
    }
    if (!run.stack.empty()) fail("Event exceeded 20,000 commands without yielding; execution stopped");
}

void Runtime::call(Run& run, const Json& cmd) {
    const auto caller = run.stack.back().context;
    const int code = cmd.at("code"), flags = arg(cmd, 1);
    if (flags & ~0x01ffffff) fail("Unsupported common-event call flags");
    int id = -1;
    if (code == 300) {
        const auto name = expand(cmd.at("strings").at(0), caller);
        for (const auto& e : project_.at("common").at("events")) if (e.at("name") == name) { id = e.at("id"); break; }
    } else {
        const int target = number(arg(cmd, 0), caller);
        if (target >= 600000 && target < 700000) fail("Encoded event-call addressing is not implemented: " + std::to_string(target));
        if (target < 500000 || target >= 600000) fail("Map-event insertion is not implemented");
        id = target - 500000;
    }
    common(id);
    const int n = flags & 15, s = (flags >> 4) & 15;
    if (n > 15 || s > 5) fail("Unsupported argument count");
    const std::size_t expected = 2 + n + s + ((flags & 0x01000000) ? 1 : 0);
    if (cmd.at("args").size() != expected) fail("Unexpected common-event call argument layout");
    std::vector<int> nums; std::vector<std::string> strs;
    for (int i = 0; i < n; ++i) nums.push_back(number(arg(cmd, 2 + i), caller));
    for (int i = 0; i < s; ++i) {
        const int slot = n + i;
        const int prefix = code == 300 || cmd.at("strings").size() == static_cast<std::size_t>(s + 1) ? 1 : 0;
        strs.push_back((flags & (1 << (12 + i))) ? expand(cmd.at("strings").at(i + prefix), caller)
            : text(arg(cmd, 2 + slot), caller));
    }
    Context callee{id, caller.event, caller.page, caller.map};
    for (int i = 0; i < n; ++i) set_number(1600000 + i + (i >= 5 ? 5 : 0), nums.at(i), callee);
    for (int i = 0; i < s; ++i) set_text(1600005 + i, strs.at(i), callee);
    Frame frame; frame.context = callee;
    if (flags & 0x01000000) frame.return_target = arg(cmd, expected - 1);
    if (run.stack.size() >= 128) fail("Common-event call depth exceeded 128");
    run.stack.push_back(std::move(frame));
}

void Runtime::picture(const Json& cmd, Context c) {
    const int flags = arg(cmd, 0), operation = flags & 15;
    const int id = number(arg(cmd, 1), c);
    const auto name = std::to_string(id);
    if (operation == 2) {
        if (cmd.at("args").size() > 3 || (flags & 0x01000000)) fail("Picture range/faded erase is not implemented");
        pictures_.erase(name); return;
    }
    if (operation != 0) fail("Picture movement and interpolation are not implemented yet");
    const int kind = (flags >> 4) & 7;
    if ((flags & 0x0ff00000) || cmd.at("args").size() != (kind == 2 || kind == 4 ? 12 : 11))
        fail("Unsupported picture transform layout");
    const auto content = kind == 2 || kind == 4 ? text(arg(cmd, 11), c) : cmd.at("strings").at(0).get<std::string>();
    pictures_[name] = {{"id", id}, {"kind", kind}, {"content", expand(content, c)},
        {"div_x", arg(cmd, 3)}, {"div_y", arg(cmd, 4)}, {"pattern", number(arg(cmd, 5), c)},
        {"opacity", number(arg(cmd, 6), c)}, {"x", number(arg(cmd, 7), c)}, {"y", number(arg(cmd, 8), c)},
        {"zoom", number(arg(cmd, 9), c)}, {"angle", number(arg(cmd, 10), c)},
        {"anchor", (flags >> 12) & 15}, {"blend", (flags >> 8) & 15}};
}

void Runtime::execute(Run& run, const Json& cmd) {
    auto& f = run.stack.back(); const auto context = f.context;
    const int code = cmd.at("code"), indent = cmd.at("indent");
    auto value = [&](std::size_t i) { return number(arg(cmd, i), context); };
    switch (code) {
    case 0: case 99: case 103: case 212: case 499: return;
    case 111: {
        std::vector<bool> conditions;
        for (int i = 0, n = arg(cmd, 0) & 15; i < n; ++i)
            conditions.push_back(compare(value(1 + 3 * i), value(2 + 3 * i), arg(cmd, 3 + 3 * i) & 255));
        branch(f, indent, conditions); return;
    }
    case 401: case 420: case 421: {
        const auto& list = commands(context);
        for (; f.pc < list.size(); ++f.pc)
            if (list.at(f.pc).at("code") == 499 && list.at(f.pc).at("indent") == indent) { ++f.pc; return; }
        fail("Missing branch end"); return;
    }
    case 121: {
        const int options = arg(cmd, 3);
        if (cmd.at("args").size() != 4 || (options & ~0xff7f)) fail("Variable range/unknown option flags are not implemented");
        auto operand = [&](int i, int literal, int indirect) {
            if (options & literal) {
                if (options & indirect) fail("Conflicting literal and indirect variable flags");
                return arg(cmd, i);
            }
            const int first = value(i);
            return options & indirect ? number(first, context) : first;
        };
        const int destination = options & 16 ? value(0) : arg(cmd, 0);
        const int a = operand(1, 4, 32), b = operand(2, 8, 64), operation = (options >> 12) & 15;
        const int assignment = (options >> 8) & 15;
        if (options & 2) {
            double computed = 0;
            switch (operation) {
            case 0: computed = static_cast<double>(a) + b; break;
            case 1: computed = static_cast<double>(a) - b; break;
            case 2: computed = static_cast<double>(a) * b; break;
            case 3: computed = b == 0 ? 0 : static_cast<double>(a) / b; break;
            case 4: computed = b == 0 ? 0 : std::fmod(a, b); break;
            case 5: computed = a & b; break;
            case 6: computed = std::uniform_int_distribution<int>(std::min(a, b), std::max(a, b))(random_); break;
            case 15: computed = b; break;
            default: fail("Unsupported real-number arithmetic operator");
            }
            const double current = number(destination, context);
            switch (assignment) {
            case 0: break; case 1: computed = current + computed; break; case 2: computed = current - computed; break;
            case 3: computed = current * computed; break; case 4: computed = computed == 0 ? 0 : current / computed; break;
            case 5: computed = computed == 0 ? 0 : std::fmod(current, computed); break;
            case 6: computed = std::max(current, computed); break; case 7: computed = std::min(current, computed); break;
            case 8: computed = std::abs(computed); break; default: fail("Unsupported real-number assignment operator");
            }
            const double bound = options & 1 ? 999999 : 2000000000;
            set_number(destination, static_cast<std::int32_t>(std::clamp(computed, -bound, bound)), context);
            return;
        }
        std::int64_t computed = 0;
        switch (operation) {
        case 0: computed = static_cast<std::int64_t>(a) + b; break;
        case 1: computed = static_cast<std::int64_t>(a) - b; break;
        case 2: computed = static_cast<std::int64_t>(a) * b; break;
        case 3: computed = b == 0 ? 0 : static_cast<std::int64_t>(a) / b; break;
        case 4: computed = b == 0 ? 0 : static_cast<std::int64_t>(a) % b; break;
        case 5: computed = a & b; break;
        case 6: computed = std::uniform_int_distribution<int>(std::min(a, b), std::max(a, b))(random_); break;
        case 15: computed = b; break;
        default: fail("Unsupported arithmetic operator " + std::to_string(operation));
        }
        int result = assign(number(destination, context), computed, assignment);
        if (options & 1) result = std::clamp(result, -999999, 999999);
        set_number(destination, result, context);
        return;
    }
    case 122:
        if (arg(cmd, 1) != 0) fail("String operations other than literal assignment are not implemented");
        set_text(arg(cmd, 0), expand(cmd.at("strings").at(0), context), context); return;
    case 123: {
        const int options = arg(cmd, 1), kind = (options >> 8) & 15;
        int pressed = 0;
        if (kind == 0) {
            if ((options & 15) != 0) {
                if (input_.down) pressed = 2; if (input_.left) pressed = 4;
                if (input_.right) pressed = 6; if (input_.up) pressed = 8;
            }
            if ((options & 16) && input_.confirm) pressed = 10;
            if ((options & 32) && input_.cancel) pressed = 11;
            if ((options & 64) && input_.sub) pressed = 12;
        } else if (kind == 3) {
            if ((options & 16) && ((options & 4096) ? input_.mouse_left : input_.mouse_left_new)) pressed = 20;
            if ((options & 32) && ((options & 4096) ? input_.mouse_right : input_.mouse_right_new)) pressed = 21;
        } else fail("Unsupported input-key kind " + std::to_string(kind));
        if (!pressed && (options & 128)) { --f.pc; run.wake = frame_ + 1; return; }
        set_number(arg(cmd, 0), pressed, context); return;
    }
    case 150: picture(cmd, context); return;
    case 201: start_route(run, cmd); return;
    case 202: run.waiting_all_routes = true; return;
    case 270: party(cmd, context); return;
    case 221: case 222: saved_variable(cmd, context); return;
    case 160:
        if (cmd.at("args").size() != 2) fail("Unexpected transition configuration layout");
        transition_ = {{"id", value(0)}, {"frames", arg(cmd, 1) & 65535},
            {"wait", ((arg(cmd, 1) >> 16) & 255) != 0}};
        return;
    case 124: {
        if (cmd.at("args").size() != 3 || ((arg(cmd, 1) >> 12) & 15) != 3 || (arg(cmd, 1) & 255))
            fail("Unsupported extended-variable query layout");
        int result = 0;
        switch (arg(cmd, 2)) {
        case 0: result = map_; break;
        case 7: result = input_.mouse_left; break;
        case 8: result = input_.mouse_right; break;
        case 9: result = input_.mouse_middle; break;
        case 10: result = input_.wheel; break;
        case 15: result = current_map()->at("width"); break;
        case 16: result = current_map()->at("height"); break;
        case 17: result = context.common; break;
        case 20: result = input_.mouse_x; break;
        case 21: result = input_.mouse_y; break;
        default: fail("Unsupported extended-variable query field " + std::to_string(arg(cmd, 2)));
        }
        set_number(arg(cmd, 0), assign(number(arg(cmd, 0), context), result, (arg(cmd, 1) >> 8) & 15), context);
        return;
    }
    case 125: {
        const int options = arg(cmd, 0);
        if (options & ~255) fail("Automatic keyboard/mouse input is not implemented");
        input_.confirm |= (options & 1) != 0; input_.cancel |= (options & 2) != 0;
        input_.sub |= (options & 4) != 0; input_.down |= (options & 16) != 0;
        input_.left |= (options & 32) != 0; input_.right |= (options & 64) != 0; input_.up |= (options & 128) != 0;
        return;
    }
    case 250: {
        const int options = arg(cmd, 3), bank = (options >> 8) & 15, operation = (options >> 12) & 15;
        if ((options & 15) || bank > 2 || operation > 1) fail("Unsupported database operation mode");
        const char* names[] = {"mutable", "system", "user"};
        int type_id = value(0), row_id = value(1), field_id = value(2);
        const int named = (options >> 16) & 7;
        const auto& original_types = project_.at("databases").at(names[bank]).at("types");
        if (named && cmd.at("strings").size() != 4) fail("Named database command has no name table");
        if (named & 1) {
            const std::string label = cmd.at("strings").at(1);
            type_id = -1;
            for (std::size_t i = 0; i < original_types.size(); ++i)
                if (original_types.at(i).at("schema").at("name") == label) { type_id = static_cast<int>(i); break; }
            if (type_id < 0) fail("Database type name not found: " + label);
        }
        const auto& original = original_types.at(type_id);
        if (named & 2) {
            const auto& labels = original.at("schema").at("names");
            const std::string label = cmd.at("strings").at(2);
            row_id = -1;
            if (original.at("id_method") != 0) fail("Non-manual database row-name lookup is not implemented");
            for (std::size_t i = 0; i < labels.size(); ++i)
                if (labels.at(i) == label) { row_id = static_cast<int>(i); break; }
            if (row_id < 0) fail("Database row name not found: " + label);
        }
        if (named & 4) {
            const auto& labels = original.at("schema").at("fields");
            const std::string label = cmd.at("strings").at(3);
            field_id = -1;
            for (std::size_t i = 0; i < labels.size(); ++i)
                if (labels.at(i) == label) { field_id = static_cast<int>(i); break; }
            if (field_id < 0) fail("Database field name not found: " + label);
        }
        const int assignment = (options >> 4) & 15;
        auto& table = databases_.at(names[bank]).at("types").at(type_id);
        if (operation == 1 && (row_id == -3 || field_id == -3)) {
            if (cmd.at("args").size() != 5) fail("Database ID query has no destination");
            const int result = row_id == -3 && field_id == -3 ? type_id : row_id == -3 ? field_id : row_id;
            set_number(arg(cmd, 4), assign(number(arg(cmd, 4), context), result, assignment), context);
            return;
        }
        if (operation == 1 && (row_id == -1 || field_id == -1)) {
            if (cmd.at("args").size() != 5) fail("Database count query has no destination");
            const int count = row_id == -1 ? static_cast<int>(table.at("rows").size())
                : static_cast<int>(original.at("positions").size());
            set_number(arg(cmd, 4), assign(number(arg(cmd, 4), context), count, assignment), context);
            return;
        }
        if (operation == 1 && row_id >= 0 && row_id < 10000 &&
                static_cast<std::size_t>(row_id) >= table.at("rows").size() && field_id >= 0 &&
                static_cast<std::size_t>(field_id) < original.at("positions").size()) {
            // Verified with the supplied Game.exe: an absent row reads as an
            // empty value, rather than raising an interpreter exception.
            if (cmd.at("args").size() != 5) fail("Database read has no destination");
            if (original.at("positions").at(field_id).get<int>() / 1000 == 2) {
                if (assignment > 1) fail("Unsupported string database assignment");
                set_text(arg(cmd, 4), assignment ? text(arg(cmd, 4), context) : "", context);
            } else set_number(arg(cmd, 4), assign(number(arg(cmd, 4), context), 0, assignment), context);
            return;
        }
        auto& field = table.at("rows").at(row_id).at(field_id);
        if (operation == 1) {
            if (cmd.at("args").size() != 5) fail("Database read has no destination");
            if (field.is_string()) {
                if (assignment > 1) fail("Unsupported string database assignment");
                set_text(arg(cmd, 4), (assignment ? text(arg(cmd, 4), context) : "") + field.get<std::string>(), context);
            } else set_number(arg(cmd, 4), assign(number(arg(cmd, 4), context), field.get<int>(), assignment), context);
        } else {
            if (field.is_string()) {
                if (assignment > 1) fail("Unsupported string database assignment");
                const auto source = cmd.at("args").size() == 5 ? text(arg(cmd, 4), context) : expand(cmd.at("strings").at(0), context);
                field = (assignment ? field.get<std::string>() : "") + source;
            } else {
                if (cmd.at("args").size() != 5) fail("Numeric database write has no source");
                field = assign(field.get<int>(), value(4), assignment);
            }
        }
        return;
    }
    case 170: case 179: {
        const auto start = f.pc - 1;
        if (code == 179 && value(0) <= 0) {
            const auto& list = commands(context);
            while (f.pc < list.size()) {
                const auto& c = list.at(f.pc++);
                if (c.at("code") == 498 && c.at("indent") == indent) return;
            }
            fail("Missing loop end");
        }
        f.loops[start] = code == 170 ? -1 : value(0); return;
    }
    case 498: case 176: case 171: {
        const auto& list = commands(context);
        std::size_t start = f.pc - 1;
        bool found = false;
        while (start > 0) {
            --start; const int candidate = list.at(start).at("code"), depth = list.at(start).at("indent");
            if ((candidate == 170 || candidate == 179) && depth == (code == 498 ? indent : indent - 1)) { found = true; break; }
        }
        if (!found) fail("Loop control outside a loop");
        if (code == 171) {
            for (; f.pc < list.size(); ++f.pc)
                if (list.at(f.pc).at("code") == 498 && list.at(f.pc).at("indent") == list.at(start).at("indent")) { ++f.pc; break; }
            f.loops.erase(start); return;
        }
        auto position = f.loops.find(start);
        if (position == f.loops.end()) return; // A label jump entering a counted loop executes once.
        int& remaining = position->second;
        if (remaining < 0 || --remaining > 0) f.pc = start + 1;
        else f.loops.erase(start);
        return;
    }
    case 172: f.pc = commands(context).size(); return;
    case 175: quit_ = true; run.stack.clear(); return;
    case 180: run.wake = frame_ + static_cast<std::uint64_t>(std::max(0, value(0))); return;
    case 210: case 300: call(run, cmd); return;
    case 213: {
        const auto& list = commands(context); const auto label = expand(cmd.at("strings").at(0), context);
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list.at(i).at("code") == 212 && list.at(i).at("strings").at(0) == label) { f.pc = i + 1; return; }
        fail("Missing label " + label); return;
    }
    default: fail("Unsupported command " + std::to_string(code) + " at original byte offset " + std::to_string(cmd.at("offset").get<int>()));
    }
}

Json Runtime::take_audio() { Json result = std::move(audio_); audio_ = Json::array(); return result; }

Json Runtime::diagnostics() const {
    Json counts = Json::object();
    for (auto [code, count] : executed_) counts[std::to_string(code)] = count;
    Json stacks = Json::array();
    for (const auto& run : runs_) {
        Json frames = Json::array();
        for (const auto& f : run.stack) frames.push_back({{"common", f.context.common}, {"event", f.context.event},
            {"map", f.context.map}, {"page", f.context.page}, {"pc", f.pc}});
        if (!frames.empty()) stacks.push_back({{"owner", run.owner}, {"frames", frames}});
    }
    return {{"frame", frame_}, {"map", map_}, {"error", error_}, {"project_id", project_id_},
        {"context", {{"common", error_context_.common}, {"event", error_context_.event}, {"page", error_context_.page}}},
        {"command", error_command_}, {"executed", counts}, {"pictures", pictures_.size()}, {"stacks", stacks}};
}

Json Runtime::snapshot() const {
    if (!error_.empty()) fail("Cannot save a halted interpreter; unsupported command state must not be skipped");
    Json numbers = Json::object(), strings = Json::object(), runs = Json::array();
    for (auto [key, value] : numbers_) numbers[std::to_string(key)] = value;
    for (const auto& [key, value] : strings_) strings[std::to_string(key)] = value;
    for (const auto& run : runs_) {
        Json stack = Json::array();
        for (const auto& f : run.stack) {
            Json loops = Json::object();
            for (auto [pc, count] : f.loops) loops[std::to_string(pc)] = count;
            stack.push_back({{"common", f.context.common}, {"event", f.context.event}, {"page", f.context.page},
                {"map", f.context.map}, {"pc", f.pc}, {"return_target", f.return_target}, {"loops", loops}});
        }
        runs.push_back({{"owner", run.owner}, {"blocking", run.blocking}, {"wake", run.wake},
            {"waiting_route", run.waiting_route}, {"waiting_all_routes", run.waiting_all_routes}, {"stack", stack}});
    }
    Json routes = Json::array();
    for (const auto& route : routes_) routes.push_back({{"id", route.id}, {"wake", route.wake}, {"pc", route.pc},
        {"target", route.target}, {"source", route.source}, {"common", route.context.common},
        {"event", route.context.event}, {"page", route.context.page}, {"map", route.context.map}});
    std::ostringstream rng; rng << random_;
    return {{"format", "wolf-ios-state"}, {"version", 2}, {"title", project_.at("game").at("title")}, {"project_id", project_id_},
        {"frame", frame_}, {"map", map_}, {"hero_x", hero_x_}, {"hero_y", hero_y_},
        {"numbers", numbers}, {"strings", strings}, {"databases", databases_}, {"pictures", pictures_},
        {"runs", runs}, {"rng", rng.str()}, {"message", message_}, {"transition", transition_},
        {"characters", characters_}, {"party", party_}, {"routes", routes}, {"next_route", next_route_}};
}

void Runtime::restore(const Json& state) {
    if (state.at("format") != "wolf-ios-state" || state.at("version") != 2 ||
        state.at("title") != project_.at("game").at("title") || state.at("project_id") != project_id_)
        fail("Incompatible native save: source game data differs");
    Runtime candidate(project_, save_root_);
    candidate.frame_ = state.at("frame"); candidate.map_ = state.at("map");
    candidate.hero_x_ = state.at("hero_x"); candidate.hero_y_ = state.at("hero_y");
    candidate.numbers_.clear();
    for (const auto& [key, value] : state.at("numbers").items()) candidate.numbers_[std::stoull(key)] = value;
    for (const auto& [key, value] : state.at("strings").items()) candidate.strings_[std::stoull(key)] = value;
    candidate.databases_ = state.at("databases"); candidate.pictures_ = state.at("pictures");
    candidate.transition_ = state.at("transition");
    candidate.characters_ = state.at("characters"); candidate.party_ = state.at("party"); candidate.next_route_ = state.at("next_route");
    for (const auto& item : state.at("routes")) {
        Route route; route.id = item.at("id"); route.wake = item.at("wake"); route.pc = item.at("pc");
        route.target = item.at("target"); route.source = item.at("source");
        route.context = {item.at("common"), item.at("event"), item.at("page"), item.at("map")};
        if (!candidate.characters_.contains(route.target) || route.pc > route.source.at("route").at("commands").size() ||
            route.id >= candidate.next_route_) fail("Save contains an invalid character route");
        candidate.routes_.push_back(std::move(route));
    }
    if (!candidate.current_map()) fail("Saved map is missing");
    for (const auto& entry : state.at("runs")) {
        Run run; run.owner = entry.at("owner"); run.blocking = entry.at("blocking"); run.wake = entry.at("wake");
        run.waiting_route = entry.at("waiting_route"); run.waiting_all_routes = entry.at("waiting_all_routes");
        for (const auto& item : entry.at("stack")) {
            Frame f; f.context = {item.at("common"), item.at("event"), item.at("page"), item.at("map")};
            f.pc = item.at("pc"); f.return_target = item.at("return_target");
            if (f.pc > candidate.commands(f.context).size()) fail("Save contains an invalid event program counter");
            for (const auto& [key, value] : item.at("loops").items()) f.loops[std::stoull(key)] = value;
            run.stack.push_back(std::move(f));
        }
        candidate.runs_.push_back(std::move(run));
    }
    std::istringstream rng(state.at("rng").get<std::string>());
    if (!(rng >> candidate.random_)) fail("Save contains invalid RNG state");
    candidate.message_ = state.at("message");
    *this = std::move(candidate);
}
}
