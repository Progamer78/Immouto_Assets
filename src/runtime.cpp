#include "runtime.hpp"
#include "text.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <regex>

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

Runtime::Runtime(Json project, std::string save_root, std::string asset_root, std::string resource_root) :
    Runtime(std::make_shared<const Json>(std::move(project)), std::move(save_root), std::move(asset_root), std::move(resource_root), {}) {}

Runtime::Runtime(std::shared_ptr<const Json> project, std::string save_root, std::string asset_root, std::string resource_root, std::string source_identity) : project_(std::move(project)),
    save_root_(std::move(save_root)), asset_root_(std::move(asset_root)), resource_root_(resource_root.empty() ? save_root_ : std::move(resource_root)),
    resources_(std::make_shared<Resources>(asset_root_, resource_root_)) {
    if (project_->at("format") != "wolf-ios-ir" || project_->at("version") != 1)
        fail("Unsupported intermediate data version");
    if (source_identity.empty()) {
        std::uint64_t fingerprint = 14695981039346656037ULL;
        for (unsigned char byte : project_->dump()) { fingerprint ^= byte; fingerprint *= 1099511628211ULL; }
        std::ostringstream identity; identity << std::hex << fingerprint; project_id_ = identity.str();
    } else project_id_ = std::move(source_identity);
    databases_ = project_->at("databases");
    const auto& start = databases_.at("system").at("types").at(7).at("rows").at(0);
    map_ = start.at(0); hero_x_ = start.at(1).get<int>() * 2; hero_y_ = start.at(2).get<int>() * 2;
    if (!current_map()) fail("Initial map is absent from the converted data");
    // Engine-owned values documented in WOLF's system-variable list.
    numbers_[9000115] = 319; // This compatibility target is the supplied 3.191 runtime.
    numbers_[9000116] = project_->at("game").at("width");
    numbers_[9000117] = project_->at("game").at("height");
    numbers_[9000118] = project_->at("game").value("tile_size", 16);
    numbers_[9000084] = project_->at("game").at("width") == 320 && project_->at("game").at("height") == 240 ? 0 :
        project_->at("game").at("width") == 640 && project_->at("game").at("height") == 480 ? 1 :
        project_->at("game").at("width") == 800 && project_->at("game").at("height") == 600 ? 2 : -1;
    for (int ref = 9000100; ref <= 9000102; ++ref) numbers_[ref] = 100;
    numbers_[9000020] = -1;
    numbers_[9000014] = 1; numbers_[9000015] = 16; numbers_[9000016] = 5;
    numbers_[9000026] = numbers_[9000027] = project_->at("game").contains("flags") ? project_->at("game").at("flags").at(12).get<int>() : 14;
}

void Runtime::fail(const std::string& message) const { throw std::runtime_error(message); }

const Json& Runtime::common(int id) const {
    const auto& events = project_->at("common").at("events");
    if (id >= 0 && static_cast<std::size_t>(id) < events.size() && events.at(id).at("id") == id) return events.at(id);
    for (const auto& event : events) if (event.at("id") == id) return event;
    fail("Missing common event " + std::to_string(id));
    return events;
}

const Json* Runtime::current_map() const {
    return map_data(map_);
}

const Json* Runtime::map_data(int id) const {
    const auto& rows = databases_.at("system").at("types").at(0).at("rows");
    if (id < 0 || static_cast<std::size_t>(id) >= rows.size()) return nullptr;
    const std::string path = rows.at(id).at(0);
    for (const auto& candidate : project_->at("maps")) if (candidate.at("file") == path) return &candidate;
    return nullptr;
}

const Json& Runtime::commands(Context c) const {
    if (c.common >= 0) return common(c.common).at("commands");
    const auto& rows = databases_.at("system").at("types").at(0).at("rows");
    const std::string file = rows.at(c.map).at(0);
    for (const auto& m : project_->at("maps")) if (m.at("file") == file)
        for (const auto& e : m.at("events")) if (e.at("id") == c.event)
            return e.at("pages").at(c.page).at("commands");
    fail("Missing command list");
    return *project_;
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
    if (!number_freezes_.empty()) {
        const auto frozen = number_freezes_.find(ref);
        if (frozen != number_freezes_.end()) return frozen->second;
    }
    if (ref >= 1000000000) {
        auto& value = cell(ref);
        if (!value.is_number_integer()) fail("Numeric read of a string database field");
        return value.get<std::int32_t>();
    }
    if (ref >= 8000000 && ref < 9000000)
        return std::uniform_int_distribution<int>(0, ref - 8000000)(random_);
    if (ref == 9000071) return input_.mouse_x;
    if (ref == 9000072) return input_.mouse_y;
    if (ref == 9000029) return static_cast<std::int32_t>(play_time_ticks_ / project_->at("game").at("fps").get<int>());
    if (ref == 9000030) return static_cast<std::int32_t>((play_time_ticks_ % project_->at("game").at("fps").get<int>()) * 1000 / project_->at("game").at("fps").get<int>());
    if (ref == 9000013) return std::any_of(runs_.begin(), runs_.end(), [](const Run& run) { return run.blocking && !run.stack.empty(); });
    const auto position = numbers_.find(key(ref, c));
    return position == numbers_.end() ? 0 : position->second;
}

void Runtime::set_number(int ref, std::int32_t value, Context c) {
    if (!number_freezes_.empty()) {
        const auto frozen = number_freezes_.find(ref);
        if (frozen != number_freezes_.end()) value = frozen->second;
    }
    if (ref == 9000029) { play_time_ticks_ = static_cast<std::uint64_t>(std::max(0, value)) * project_->at("game").at("fps").get<int>(); return; }
    if (ref >= 1000000000) {
        auto& field = cell(ref);
        if (!field.is_number_integer()) fail("Numeric write to a string database field");
        field = value;
    } else numbers_[key(ref, c)] = value;
}

void Runtime::set_number_freezes(const std::map<int, std::int32_t>& values) {
    for (const auto& [ref, value] : values) {
        (void)value;
        if (ref < 2000000 || ref >= 3000000) fail("Only global numeric game variables can be frozen");
    }
    number_freezes_ = values;
    for (const auto& [ref, value] : values) numbers_[static_cast<unsigned>(ref)] = value;
}

std::vector<int> Runtime::numeric_variable_refs() const {
    std::vector<int> refs;
    for (const auto& [ref, value] : numbers_) {
        (void)value;
        if (ref >= 2000000 && ref < 3000000) refs.push_back(static_cast<int>(ref));
    }
    return refs;
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
    // Substitute all leaf references in a pass so the limit bounds nesting,
    // rather than the number of independent variables in a long message.
    static const std::regex reference(R"(\\(v[0-9]?|s|cself|self|sys|syss|udb|cdb|sdb)\[(-?[0-9]+(?::-?[0-9]+)*)\])", std::regex::icase);
    for (int pass = 0; pass < 16; ++pass) {
        if (value.find('\\') == std::string::npos) return value;
        bool changed = false;
        std::string output; std::size_t previous = 0;
        for (std::sregex_iterator it(value.begin(), value.end(), reference), end; it != end; ++it) {
            const auto& match = *it; const auto start = static_cast<std::size_t>(match.position());
            output.append(value, previous, start - previous);
            previous = start + match.length();
            std::size_t escapes = 0;
            for (auto before = start; before > 0 && value[before - 1] == '\\'; --before) ++escapes;
            if (escapes % 2) { output += match.str(); continue; }
            std::string tag = match[1];
            std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char ch) { return std::tolower(ch); });
            std::vector<int> indices; std::istringstream arguments(match[2].str()); std::string part;
            while (std::getline(arguments, part, ':')) indices.push_back(std::stoi(part));
            if (std::any_of(indices.begin(), indices.end(), [](int index) { return index < 0 || index > 99999; })) fail("Invalid substitution index");
            std::string replacement;
            if (tag == "udb" || tag == "cdb" || tag == "sdb") {
                if (indices.size() > 3) fail("Invalid database substitution");
                const auto& type = databases_.at(tag == "udb" ? "user" : tag == "cdb" ? "mutable" : "system").at("types").at(indices.at(0));
                if (indices.size() == 1) replacement = type.at("schema").at("name");
                else if (indices.size() == 2) replacement = type.at("schema").at("names").at(indices.at(1));
                else {
                    const auto& field = type.at("rows").at(indices.at(1)).at(indices.at(2));
                    replacement = field.is_string() ? field.get<std::string>() : std::to_string(field.get<int>());
                }
            } else {
                if (indices.size() != 1) fail("Invalid variable substitution");
                const int index = indices.at(0);
                const int bank = tag.size() == 2 && tag.front() == 'v' ? tag[1] - '0' : 0;
                const int ref = tag.front() == 'v' ? 2000000 + bank * 100000 + index : tag == "s" ? 3000000 + index :
                    tag == "sys" ? 9000000 + index : tag == "syss" ? 9900000 + index : tag == "self" ? 1100000 + index : 1600000 + index;
                const bool string = tag == "s" || tag == "syss" || (tag == "cself" && index >= 5 && index <= 9);
                replacement = string ? text(ref, c) : std::to_string(number(ref, c));
            }
            output += replacement; changed = true;
            if (output.size() > 16 * 1024 * 1024) fail("Expanded game text is too large");
        }
        output.append(value, previous, value.size() - previous);
        if (!changed) return value;
        value = std::move(output);
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
    for (const auto& event : project_->at("common").at("events")) {
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
    raw_input_ = input;
    const std::array<bool Input::*, 7> auto_buttons{&Input::confirm, &Input::cancel, &Input::sub, &Input::down, &Input::left, &Input::right, &Input::up};
    const std::array<int, 7> auto_bits{1, 2, 4, 16, 32, 64, 128};
    for (std::size_t i = 0; i < auto_buttons.size(); ++i) raw_input_.*auto_buttons[i] |= (pending_auto_input_ & auto_bits[i]) != 0;
    pending_auto_input_ = 0;
    auto_mouse_input_ = pending_auto_mouse_input_;
    pending_auto_mouse_input_ = 0;
    apply_input_permissions();
    try {
        if (!file_operations_.empty()) fail("Pending game file operation requires the native renderer");
        for (auto it = pending_audio_.begin(); it != pending_audio_.end();) {
            if (it->at("start").get<std::uint64_t>() <= frame_) { audio_.push_back(it->at("event")); it = pending_audio_.erase(it); }
            else ++it;
        }
        if (choice_clear_after_ && frame_ >= choice_clear_after_) { set_number(9000020, -1); choice_clear_after_ = 0; }
        advance_pictures();
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
            if (!pending_game_load_.empty() || pending_title_reset_) break;
        }
        if (!pending_game_load_.empty()) { apply_game_load(); return; }
        if (pending_title_reset_) { reset_to_title(); return; }
        runs_.erase(std::remove_if(runs_.begin(), runs_.end(), [&](const Run& r) {
            return r.stack.empty() || (!r.blocking && r.stack.front().context.common < 0 && r.stack.front().context.map != map_);
        }), runs_.end());
        advance_choice();
        advance_message();
        previous_input_ = input_;
        ++frame_;
        numbers_[9000028] = (static_cast<std::int64_t>(number(9000028)) + 1 + 10000000) % 10000000;
        ++play_time_ticks_;
    } catch (const std::exception& error) { error_ = error.what(); }
    try { flush_saves(); } catch (const std::exception& error) { error_ = error.what(); }
}

void Runtime::apply_input_permissions() {
    input_ = raw_input_;
    const std::array<bool Input::*, 7> buttons{&Input::confirm, &Input::cancel, &Input::sub, &Input::down, &Input::left, &Input::right, &Input::up};
    for (std::size_t i = 0; i < buttons.size(); ++i) if (input_permissions_[i] == 2) input_.*buttons[i] = false;
    if (!mouse_enabled_) {
        input_.mouse_left = input_.mouse_right = input_.mouse_middle = false;
        input_.mouse_left_new = input_.mouse_right_new = false; input_.wheel = 0;
        input_.mouse_x = previous_input_.mouse_x; input_.mouse_y = previous_input_.mouse_y;
    }
    // Device permissions block player input. Like the Windows engine, an
    // event's simulated mouse buttons still apply for this frame. Consecutive
    // simulated presses remain held rather than producing fresh click edges.
    input_.mouse_left |= (auto_mouse_input_ & 1) != 0;
    input_.mouse_right |= (auto_mouse_input_ & 2) != 0;
    input_.mouse_middle |= (auto_mouse_input_ & 4) != 0;
    input_.mouse_left_new |= (auto_mouse_input_ & 1) && !previous_input_.mouse_left;
    input_.mouse_right_new |= (auto_mouse_input_ & 2) && !previous_input_.mouse_right;
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
        if (!pending_game_load_.empty() || pending_title_reset_) return;
        if (run.wake > frame_ || route_pending(run)) return;
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
        for (const auto& e : project_->at("common").at("events")) if (e.at("name") == name) { id = e.at("id"); break; }
    } else {
        int target = number(arg(cmd, 0), caller);
        if (target >= 600000) {
            if (caller.common < 0) fail("Relative common call outside a common event is not verified");
            // The original engine encodes relative IDs around 600100, not a
            // variable address. 600094 from common 270 therefore calls 264.
            const auto relative = static_cast<std::int64_t>(caller.common) + target - 600100;
            const auto last = static_cast<std::int64_t>(project_->at("common").at("events").size()) - 1;
            target = 500000 + static_cast<int>(std::clamp(relative, std::int64_t{0}, last));
        }
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
    if (operation == 1) { move_picture(cmd, c); return; }
    if (operation == 3) {
        const bool range = (flags & 0x01000000) != 0;
        if (cmd.at("args").size() != (range ? 3 : 2) || (flags & ~0x07ffffff)) fail("Unsupported picture delay-reset layout");
        const int last = range ? number(arg(cmd, 2), c) : id;
        const int first_id = std::min(id, last), last_id = std::max(id, last);
        Json retained = Json::array();
        for (const auto& pending : pending_picture_erases_) {
            const int low = std::min(pending.at("id").get<int>(), pending.value("last", pending.at("id").get<int>()));
            const int high = std::max(pending.at("id").get<int>(), pending.value("last", pending.at("id").get<int>()));
            if (high < first_id || low > last_id) retained.push_back(pending);
            else {
                if (low < first_id) { auto left = pending; left["id"] = low; left["last"] = first_id - 1; retained.push_back(std::move(left)); }
                if (high > last_id) { auto right = pending; right["id"] = last_id + 1; right["last"] = high; retained.push_back(std::move(right)); }
            }
        }
        pending_picture_erases_ = std::move(retained);
        retained = Json::array();
        for (const auto& pending : pending_picture_changes_) {
            const auto& action = pending.at("action");
            const int low = std::min(action.at("id").get<int>(), action.value("last", action.at("id").get<int>()));
            const int high = std::max(action.at("id").get<int>(), action.value("last", action.at("id").get<int>()));
            if (high < first_id || low > last_id) retained.push_back(pending);
            else {
                if (low < first_id) { auto left = pending; left["action"]["id"] = low; left["action"]["last"] = first_id - 1; retained.push_back(std::move(left)); }
                if (high > last_id) { auto right = pending; right["action"]["id"] = last_id + 1; right["action"]["last"] = high; retained.push_back(std::move(right)); }
            }
        }
        pending_picture_changes_ = std::move(retained);
        return;
    }
    if (operation == 2) {
        const bool range = (flags & 0x01000000) != 0;
        if (cmd.at("args").size() < 3 || cmd.at("args").size() > (range ? 6 : 4) ||
            (range && (cmd.at("args").size() != 6 || arg(cmd, 4) != 0)) || (flags & ~0x07ffffff))
            fail("Unsupported picture erase range/options");
        const int duration = std::max(0, number(arg(cmd, 2), c));
        const int delay = cmd.at("args").size() >= 4 ? std::max(0, number(arg(cmd, 3), c)) : 0;
        const int last = range ? number(arg(cmd, 5), c) : id;
        if (delay) pending_picture_erases_.push_back({{"id", id}, {"last", last}, {"duration", duration}, {"start", frame_ + delay}});
        else erase_picture_range(id, last, duration);
        return;
    }
    if (operation != 0) fail("Picture movement and interpolation are not implemented yet");
    const int kind = (flags >> 4) & 15;
    const bool variable_file = kind == 1 || kind == 4;
    const auto count = cmd.at("args").size();
    const int zoom_mode = (flags >> 20) & 15;
    if ((flags & ~0x00f1ffff) || (zoom_mode != 0 && zoom_mode != 3 && zoom_mode != 4) || kind > 4 ||
        (count != (variable_file ? 12u : 11u) && count != 13 && count != 14 && count != 15 && count != 18 && count != 19) ||
        (count >= 15 && arg(cmd, 14) != 0) || (zoom_mode == 3 && count != 19))
        fail("Unsupported picture transform layout");
    const int duration = std::max(0, number(arg(cmd, 2), c));
    const auto content = variable_file ? text(arg(cmd, 11), c) : cmd.at("strings").at(0).get<std::string>();
    Json record = {{"id", id}, {"kind", kind}, {"content", expand(content, c)},
        {"div_x", number(arg(cmd, 3), c)}, {"div_y", number(arg(cmd, 4), c)}, {"pattern", number(arg(cmd, 5), c)},
        {"opacity", number(arg(cmd, 6), c)}, {"x", number(arg(cmd, 7), c)}, {"y", number(arg(cmd, 8), c)},
        {"zoom", number(arg(cmd, 9), c)}, {"angle", number(arg(cmd, 10), c)},
        {"anchor", (flags >> 12) & 15}, {"blend", (flags >> 8) & 15}};
    record["zoom_y"] = zoom_mode == 3 ? number(arg(cmd, 18), c) : record.at("zoom").get<int>();
    const int color_mode = count >= 13 ? static_cast<unsigned int>(arg(cmd, 12)) >> 24 : 0;
    if ((count >= 13 && (arg(cmd, 12) & 0xffffff)) || color_mode > 2 || (color_mode == 2 && count < 18)) fail("Unsupported picture color layout");
    for (int i = 0; i < 3; ++i) record[std::array<const char*, 3>{"red", "green", "blue"}[i]] = color_mode == 2 ? number(arg(cmd, 15 + i), c) : 100;
    if (kind == 2) {
        record["text_style"] = {{"size", number(9000008)}, {"shadow", number(9000021)},
            {"speed", 0}, {"antialias", project_->at("game").value("flags", Json::array()).size() > 14 ?
                project_->at("game").at("flags").at(14).get<int>() : 0}};
        record["text_reveal"] = {{"times", text_reveal_times(record.at("content"), number(9000011))}};
    }
    const Json action = {{"id", id}, {"operation", 0}, {"duration", duration}, {"values", record},
        {"same_zoom", zoom_mode == 4}, {"same_color", color_mode == 1}, {"relative", (flags & 65536) != 0}};
    const int delay = count >= 14 ? std::max(0, number(arg(cmd, 13), c)) : 0;
    if (delay) {
        // Game.exe allocates an empty picture immediately. Queries see it and
        // effects attach to it before its delayed image/text becomes visible.
        const auto name = std::to_string(id);
        if (!pictures_.contains(name)) pictures_[name] = {{"id", id}, {"kind", -1}, {"content", ""},
            {"div_x", 1}, {"div_y", 1}, {"pattern", 1}, {"opacity", 0}, {"x", 0}, {"y", 0},
            {"zoom", 100}, {"zoom_y", 100}, {"angle", 0}, {"anchor", 0}, {"blend", 0}, {"red", 100}, {"green", 100}, {"blue", 100}};
        pending_picture_changes_.push_back({{"start", frame_ + delay}, {"action", action}});
    } else apply_picture_change(action);
}

void Runtime::move_picture(const Json& cmd, Context context) {
    const int id = number(arg(cmd, 1), context);
    const int flags = arg(cmd, 0);
    const auto count = cmd.at("args").size();
    const int zoom_mode = (flags >> 20) & 15;
    const bool range = (flags & 0x01000000) != 0;
    if ((flags & ~0x01f1ffff) || (zoom_mode != 0 && zoom_mode != 3 && zoom_mode != 4) ||
        (count != 11 && count != 13 && count != 14 && count != 15 && count != 18 && count != 19) ||
        (count >= 13 && arg(cmd, 11) != 0) || (range && count < 15) ||
        (!range && count >= 15 && arg(cmd, 14) != 0) || (zoom_mode == 3 && count != 19))
        fail("Unsupported picture movement layout");
    Json values = Json::object();
    for (const auto& [field, index] : std::vector<std::pair<std::string, int>>{{"pattern", 5}, {"opacity", 6}, {"x", 7}, {"y", 8}, {"zoom", 9}, {"angle", 10}}) {
        if (field == "zoom" && zoom_mode == 4) continue;
        const int value = number(arg(cmd, index), context);
        if (value == -1000000) continue;
        values[field] = value;
    }
    if (zoom_mode == 3) values["zoom_y"] = number(arg(cmd, 18), context);
    else if (values.contains("zoom")) values["zoom_y"] = values.at("zoom");
    const int color_mode = count >= 13 ? static_cast<unsigned int>(arg(cmd, 12)) >> 24 : 1;
    if ((count >= 13 && (arg(cmd, 12) & 0xffffff)) || color_mode > 2 || (color_mode == 2 && count < 18)) fail("Unsupported picture movement color layout");
    if (color_mode != 1) for (int i = 0; i < 3; ++i) values[std::array<const char*, 3>{"red", "green", "blue"}[i]] = color_mode == 2 ? number(arg(cmd, 15 + i), context) : 100;
    // The editor retains an anchor nibble in Move records, but Game.exe
    // ignores it: only Show can change a picture's anchor.
    const int blend = (flags >> 8) & 15;
    if (blend != 15) values["blend"] = blend;
    const int duration = std::max(0, number(arg(cmd, 2), context));
    Json action = {{"id", id}, {"operation", 1}, {"duration", duration}, {"values", values}, {"relative", (flags & 65536) != 0}};
    if (range) action["last"] = number(arg(cmd, 14), context);
    const int delay = count >= 14 ? std::max(0, number(arg(cmd, 13), context)) : 0;
    if (delay) pending_picture_changes_.push_back({{"start", frame_ + delay}, {"action", action}});
    else apply_picture_change(action);
}

void Runtime::apply_picture_change(const Json& action) {
    if (action.contains("last")) {
        const int first = action.at("id"), last = action.at("last");
        std::vector<int> selected;
        for (const auto& picture : pictures_) {
            const int id = picture.at("id");
            if (id >= std::min(first, last) && id <= std::max(first, last)) selected.push_back(id);
        }
        auto single = action; single.erase("last");
        for (int id : selected) { single["id"] = id; apply_picture_change(single); }
        return;
    }
    const auto id = std::to_string(action.at("id").get<int>());
    const bool show = action.at("operation") == 0;
    if (!show && !pictures_.contains(id)) return;
    const Json previous = pictures_.contains(id) ? pictures_.at(id) : Json::object();
    auto target = show ? action.at("values") : previous;
    target.erase("tween"); target.erase("fade_erase");
    if (show) {
        if (action.value("relative", false)) for (const char* field : {"x", "y"})
            if (target.at(field) != -1000000) target[field] = previous.value(field, 0) + target.at(field).get<int>();
        // Show also replaces an existing picture. The editor's "same value"
        // fields retain its state just as they do for a move, including blend.
        for (const auto& [field, fallback] : std::vector<std::pair<std::string, int>>{
                {"pattern", 1}, {"opacity", 255}, {"x", 0}, {"y", 0},
                {"zoom", 100}, {"zoom_y", 100}, {"angle", 0}})
            if (target.at(field) == -1000000) target[field] = previous.value(field, fallback);
        for (const char* field : {"anchor", "blend"})
            if (target.at(field) == 15) target[field] = previous.value(field, 0);
        if (previous.contains("flicker")) target["flicker"] = previous.at("flicker");
        if (previous.contains("draw_shift")) target["draw_shift"] = previous.at("draw_shift");
        if (previous.contains("zoom_effect")) target["zoom_effect"] = previous.at("zoom_effect");
        if (previous.contains("automatic_zoom")) target["automatic_zoom"] = previous.at("automatic_zoom");
        if (previous.contains("shake")) target["shake"] = previous.at("shake");
        if (previous.contains("pattern_effect")) target["pattern_effect"] = previous.at("pattern_effect");
        if (previous.contains("color_correction")) target["color_correction"] = previous.at("color_correction");
        if (previous.contains("flash")) target["flash"] = previous.at("flash");
        if (action.at("same_zoom") == true && !previous.empty()) { target["zoom"] = previous.at("zoom"); target["zoom_y"] = previous.at("zoom_y"); }
        if (action.at("same_color") == true && !previous.empty()) for (const char* color : {"red", "green", "blue"}) target[color] = previous.value(color, 100);
        if (target.contains("text_reveal")) {
            target["text_reveal"]["start"] = frame_;
            int visible = 0;
            for (const auto& time : target.at("text_reveal").at("times")) { if (time.get<double>() > 0) break; ++visible; }
            target["text_style"]["visible_chars"] = visible;
        }
    } else for (const auto& [field, value] : action.at("values").items()) {
        if (field == "pattern" && target.at("kind").get<int>() >= 2) continue;
        target[field] = action.at("relative") == true && (field == "x" || field == "y") ? Json(previous.at(field).get<int>() + value.get<int>()) : value;
    }
    const int duration = action.at("duration");
    if (duration) {
        Json from = Json::object(), to = Json::object();
        for (const char* field : {"opacity", "x", "y", "zoom", "zoom_y", "angle", "red", "green", "blue"}) {
            to[field] = target.at(field);
            from[field] = previous.contains(field) ? previous.at(field) : std::string(field) == "opacity" ? Json(0) : target.at(field);
            target[field] = from.at(field);
        }
        target["tween"] = {{"start", frame_}, {"duration", duration}, {"from", from}, {"to", to}};
    }
    pictures_[id] = std::move(target);
}

void Runtime::erase_picture(int id, int duration) {
    const auto name = std::to_string(id);
    if (!pictures_.contains(name)) return;
    if (duration == 0) { pictures_.erase(name); return; }
    auto& picture = pictures_.at(name);
    picture["fade_erase"] = {{"start", frame_}, {"duration", duration}, {"opacity", picture.at("opacity")}};
}

void Runtime::erase_picture_range(int first, int last, int duration) {
    std::vector<int> selected;
    for (const auto& picture : pictures_) {
        const int id = picture.at("id");
        if (id >= std::min(first, last) && id <= std::max(first, last)) selected.push_back(id);
    }
    for (int id : selected) erase_picture(id, duration);
}

void Runtime::advance_pictures() {
    for (auto it = pending_picture_changes_.begin(); it != pending_picture_changes_.end();) {
        if (it->at("start").get<std::uint64_t>() <= frame_) {
            apply_picture_change(it->at("action")); it = pending_picture_changes_.erase(it);
        } else ++it;
    }
    for (auto it = pending_picture_erases_.begin(); it != pending_picture_erases_.end();) {
        if (it->at("start").get<std::uint64_t>() <= frame_) {
            erase_picture_range(it->at("id"), it->value("last", it->at("id").get<int>()), it->at("duration"));
            it = pending_picture_erases_.erase(it);
        } else ++it;
    }
    for (auto it = pictures_.begin(); it != pictures_.end();) {
        auto& picture = it.value();
        if (picture.contains("pattern_effect")) {
            const auto& effect = picture.at("pattern_effect");
            const auto start = effect.at("start").get<std::uint64_t>();
            const auto interval = effect.at("interval").get<std::uint64_t>();
            const int first = effect.at("first"), last = effect.at("last");
            if (effect.at("type") == 8 && frame_ >= start &&
                (frame_ - start) / interval >= static_cast<unsigned>(std::abs(last - first) + 1)) {
                // In the supplied 3.191 engine a one-shot animation commits
                // its final pattern. Loop/ping-pong effects leave the base alone.
                picture["pattern"] = last; picture.erase("pattern_effect");
            }
        }
        if (picture.contains("draw_shift") && picture.at("draw_shift").contains("tween")) {
            auto& shift = picture["draw_shift"]; const auto& tween = shift.at("tween");
            const auto age = std::min(frame_ - tween.at("start").get<std::uint64_t>(), tween.at("duration").get<std::uint64_t>());
            for (const char* field : {"x", "y"}) {
                const auto from = tween.at("from").at(field).get<std::int64_t>();
                shift[field] = from + (tween.at("to").at(field).get<std::int64_t>() - from) * static_cast<std::int64_t>(age) / tween.at("duration").get<std::int64_t>();
            }
            if (age == tween.at("duration")) shift.erase("tween");
        }
        if (picture.contains("text_reveal")) {
            const auto& reveal = picture.at("text_reveal");
            const double age = static_cast<double>(frame_ - reveal.at("start").get<std::uint64_t>()) / project_->at("game").at("fps").get<int>();
            int visible = 0;
            for (const auto& time : reveal.at("times")) { if (time.get<double>() > age + 1e-9) break; ++visible; }
            picture["text_style"]["visible_chars"] = visible;
        }
        if (picture.contains("tween")) {
            const auto& tween = picture.at("tween");
            const auto start = tween.at("start").get<std::uint64_t>();
            const auto duration = tween.at("duration").get<std::uint64_t>();
            const auto age = std::min(frame_ >= start ? frame_ - start : 0, duration);
            for (const auto& [field, to] : tween.at("to").items()) {
                const auto from = tween.at("from").at(field).get<std::int64_t>();
                picture[field] = from + (to.get<std::int64_t>() - from) * static_cast<std::int64_t>(age) / static_cast<std::int64_t>(duration);
            }
            if (age == duration) picture.erase("tween");
        }
        if (!picture.contains("fade_erase")) { ++it; continue; }
        const auto& fade = picture.at("fade_erase");
        const auto start = fade.at("start").get<std::uint64_t>();
        const auto duration = fade.at("duration").get<std::uint64_t>();
        const auto age = frame_ >= start ? frame_ - start : 0;
        if (age >= duration) { it = pictures_.erase(it); continue; }
        picture["opacity"] = static_cast<int>(fade.at("opacity").get<std::int64_t>() * (duration - age) / duration);
        ++it;
    }
}

void Runtime::picture_effect(const Json& cmd, Context c) {
    const int options = arg(cmd, 0), type = (options >> 4) & 31;
    if ((options & ~511) || (options & 15) != 0 || cmd.at("args").size() != 7 ||
        (type != 0 && type != 1 && type != 2 && type != 3 && type != 4 && type != 5 && type != 6 && type != 7 && type != 8 && type != 9 && type != 10 && type != 25))
        fail("Unsupported picture-effect target or layout");
    const int duration = std::max(0, number(arg(cmd, 1), c));
    const int first = number(arg(cmd, 2), c), last = number(arg(cmd, 3), c);
    const Json color = Json::array({number(arg(cmd, 4), c), number(arg(cmd, 5), c), number(arg(cmd, 6), c)});
    if (type == 25 && (duration != 0 || color != Json::array({0, 0, 0}))) fail("Unsupported clear-picture-effects operands");
    if (type == 2 && color.at(2) != 0) fail("Unsupported picture-shift extra operand");
    if (type == 3 && color.at(2).get<int>() < 0) fail("Negative picture-shake repeat count is not implemented");
    if (type == 7 && color.at(2).get<int>()<0) fail("Negative automatic picture zoom pause is not implemented");
    // The third parameter is an inactive editor field for pattern effects.
    // This game's attack records retain 50 there; Game.exe ignores it.
    if (type >= 8 && type <= 10 && duration && (color.at(0).get<int>() < 1 || color.at(1).get<int>() < 1))
        fail("Unsupported automatic picture-pattern operands");
    for (auto& picture : pictures_) {
        const int id = picture.at("id");
        if (id < std::min(first, last) || id > std::max(first, last)) continue;
        if (type == 25) {
            // Effect state is independent of Show/Move properties and their
            // interpolation. The battle cursor clears effects this way.
            for (const char* field : {"draw_shift", "zoom_effect", "shake", "flicker", "pattern_effect", "automatic_zoom", "color_correction", "flash"}) picture.erase(field);
            continue;
        }
        if (type == 0) {
            if (!duration || color == Json::array({0, 0, 0})) picture.erase("flash");
            else picture["flash"] = {{"start", frame_ + 1}, {"duration", duration}, {"color", color}};
            continue;
        }
        if (type == 1) {
            std::array<double, 3> from{};
            if (picture.contains("color_correction")) {
                const auto& previous = picture.at("color_correction");
                const auto start = previous.at("start").get<std::uint64_t>(), time = previous.at("duration").get<std::uint64_t>();
                const double fraction = time ? static_cast<double>(std::min(frame_ >= start ? frame_ - start : 0, time)) / time : 1;
                for (int i = 0; i < 3; ++i) from[i] = previous.at("from").at(i).get<double>() +
                    (previous.at("to").at(i).get<double>() - previous.at("from").at(i).get<double>()) * fraction;
            }
            if (!duration && color == Json::array({0, 0, 0})) picture.erase("color_correction");
            else picture["color_correction"] = {{"start", frame_ + 1}, {"duration", duration}, {"from", from}, {"to", color}};
            continue;
        }
        if (type == 7) {
            if (!duration || (color.at(0)==0 && color.at(1)==0)) picture.erase("automatic_zoom");
            else picture["automatic_zoom"]={{"start",frame_+1},{"duration",duration},{"x",color.at(0)},{"y",color.at(1)},{"pause",color.at(2)}};
            continue;
        }
        if (type == 4) {
            if (color.at(2).get<int>() < 0) fail("Negative group picture zoom is not implemented");
            const auto previous = picture_group_zoom(picture, frame_);
            picture["zoom_effect"] = {{"x", color.at(0)}, {"y", color.at(1)}, {"percent", color.at(2)},
                {"from", previous[2] * 100}, {"start", frame_}, {"duration", duration}};
            continue;
        }
        if (type == 3) {
            if (duration == 0 || color.at(2) == 0 || (color.at(0) == 0 && color.at(1) == 0)) picture.erase("shake");
            else picture["shake"] = {{"start", frame_ + 1}, {"duration", duration}, {"x", color.at(0)}, {"y", color.at(1)}, {"count", color.at(2)}};
            continue;
        }
        if (type >= 8) {
            if (duration == 0) picture.erase("pattern_effect");
            else picture["pattern_effect"] = {{"type", type}, {"start", frame_}, {"interval", duration}, {"first", color.at(0)}, {"last", color.at(1)}};
            continue;
        }
        if (type == 2) {
            auto shift = picture.value("draw_shift", Json{{"x", 0}, {"y", 0}});
            shift.erase("tween");
            const Json to = {{"x", color.at(0)}, {"y", color.at(1)}};
            if (duration) shift["tween"] = {{"start", frame_}, {"duration", duration}, {"from", shift}, {"to", to}};
            else shift = to;
            picture["draw_shift"] = std::move(shift); continue;
        }
        if (duration == 0 || (color.at(0) == 0 && color.at(1) == 0 && color.at(2) == 0)) picture.erase("flicker");
        else picture["flicker"] = {{"type", type}, {"duration", duration}, {"start", frame_ + 1}, {"color", color}};
    }
}

void Runtime::show_choice(Run& run, const Json& cmd) {
    auto& frame = run.stack.back();
    const int flags = arg(cmd, 0), count = flags & 15, cancel = (flags >> 4) & 15;
    if ((flags & ~255) || cmd.at("args").size() != 1 || count < 1 || count != static_cast<int>(cmd.at("strings").size()) || cancel > count + 1)
        fail("Unsupported choice-command layout");
    if (choice_.empty()) {
        Json strings = Json::array();
        for (const auto& text : cmd.at("strings")) strings.push_back(expand(text, frame.context));
        for (std::size_t i = 0; i < strings.size() && i < 10; ++i) set_text(9900040 + static_cast<int>(i), strings.at(i));
        std::vector<int> visible;
        for (int i = 0; i < count; ++i) if (!strings.at(i).get_ref<const std::string&>().empty()) visible.push_back(i);
        if (visible.empty()) {
            const auto& list = commands(frame.context);
            while (frame.pc < list.size()) {
                const auto& row = list.at(frame.pc++);
                if (row.at("indent") == cmd.at("indent") && row.at("code") == 499) return;
            }
            fail("Empty choice has no matching end");
        }
        int selected = std::clamp(number(9000007), 0, count - 1);
        if (std::find(visible.begin(), visible.end(), selected) == visible.end()) selected = visible.front();
        choice_ = {{"owner", run.owner}, {"pc", frame.pc - 1}, {"common", frame.context.common},
            {"strings", strings}, {"selected", selected}, {"cancel", cancel},
            {"opened", frame_}, {"repeat", 0}, {"done", false},
            // The game's parallel mouse adapter queues basic Confirm on the
            // following tick. Do not let the press opening this menu (including
            // that deferred input) accept its first entry before release.
            {"armed", !(input_.confirm || input_.cancel || input_.mouse_left || input_.mouse_right ||
                input_.mouse_left_new || input_.mouse_right_new || (pending_auto_input_ & 3) || (pending_auto_mouse_input_ & 3))},
            {"x", number(9000003)}, {"y", number(9000004)}};
        set_number(9000020, choice_.at("selected"));
        prepare_choice_layout();
        choice_clear_after_ = 0;
    }
    if (choice_.at("owner") != run.owner || choice_.at("common") != frame.context.common || choice_.at("pc") != frame.pc - 1 || !choice_.at("done").get<bool>()) {
        --frame.pc; run.wake = frame_ + 1; return;
    }
    const int selected = choice_.at("selected");
    choice_ = Json::object(); set_number(9000007, 0); choice_clear_after_ = frame_ + 1;
    const int indent = cmd.at("indent");
    const auto& list = commands(frame.context);
    for (; frame.pc < list.size(); ++frame.pc) {
        const auto& candidate = list.at(frame.pc);
        if (candidate.at("indent") != indent) continue;
        const int code = candidate.at("code");
        if ((selected == -2 && code == 421) || (selected >= 0 && code == 401 && arg(candidate, 0) == selected + 2)) { ++frame.pc; return; }
        if (code == 499) break;
    }
    fail("Choice has no matching branch");
}

void Runtime::transfer(const Json& cmd, Context context) {
    if (cmd.at("args").size() != 5 || arg(cmd, 0) != -1 || (arg(cmd, 4) & ~1))
        fail("Unsupported transfer target, transition or position layout");
    const int destination = number(arg(cmd, 3), context) == -1 ? map_ : number(arg(cmd, 3), context);
    if (!map_data(destination)) fail("Transfer destination map is missing");
    const auto& settings = databases_.at("system").at("types").at(0).at("rows").at(destination);
    if (destination != map_) {
        for (int kind = 0; kind < 2; ++kind) {
            const int setting = settings.at(1 + kind);
            if (setting != -2 && setting != -1) fail("Database/filename destination-map audio is not implemented");
        }
        for (int kind = 0; kind < 2; ++kind) if (settings.at(1 + kind) == -1) {
            audio_.push_back({{"operation", "stop"}, {"kind", kind}, {"channel", -1}, {"fade_ms", 0}});
            music_.erase(std::to_string(kind));
        }
    }
    const int scale = arg(cmd, 4) & 1 ? 1 : 2;
    hero_x_ = number(arg(cmd, 1), context) * scale; hero_y_ = number(arg(cmd, 2), context) * scale;
    map_ = destination;
    const auto hero = character_key(-2, context);
    characters_.at(hero)["x"] = hero_x_; characters_.at(hero)["y"] = hero_y_;
}

void Runtime::advance_choice() {
    if (choice_.empty() || choice_.at("done") == true || choice_.at("opened").get<std::uint64_t>() == frame_) return;
    if (!choice_.value("armed", true)) {
        if (!(input_.confirm || input_.cancel || input_.mouse_left || input_.mouse_right ||
            input_.mouse_left_new || input_.mouse_right_new || (pending_auto_input_ & 3) || (pending_auto_mouse_input_ & 3))) choice_["armed"] = true;
        return;
    }
    const int count = static_cast<int>(choice_.at("strings").size());
    std::vector<int> visible;
    for (int i = 0; i < count; ++i) if (!choice_.at("strings").at(i).get_ref<const std::string&>().empty()) visible.push_back(i);
    if (visible.empty()) fail("Active choice has no visible entries");
    int selected = std::clamp(number(9000020), 0, count - 1);
    if (std::find(visible.begin(), visible.end(), selected) == visible.end()) selected = visible.front();
    int direction = input_.up ? -1 : input_.down ? 1 : 0;
    const bool new_direction = (input_.up && !previous_input_.up) || (input_.down && !previous_input_.down);
    if (input_.wheel) { direction = input_.wheel > 0 ? -1 : 1; choice_["repeat"] = frame_; }
    if (direction && (new_direction || frame_ >= choice_.at("repeat").get<std::uint64_t>())) {
        const int position = static_cast<int>(std::find(visible.begin(), visible.end(), selected) - visible.begin());
        selected = visible.at((position + direction + static_cast<int>(visible.size())) % visible.size());
        choice_sound(9000018);
        choice_["repeat"] = frame_ + std::max(1, number(new_direction ? 9000015 : 9000016));
    }
    const bool keyboard_confirm = input_.confirm && !previous_input_.confirm;
    bool done = keyboard_confirm || input_.mouse_left_new;
    if (choice_.contains("layout") && (input_.mouse_left_new || input_.mouse_x != previous_input_.mouse_x || input_.mouse_y != previous_input_.mouse_y)) {
        const auto& layout = choice_.at("layout");
        const int x = layout.at("x"), top = layout.at("y").get<int>() + layout.at("pad_y").get<int>();
        bool hit = false;
        if (input_.mouse_x >= x && input_.mouse_x < x + layout.at("width").get<int>()) {
            for (std::size_t i = 0; i < layout.at("rows").size(); ++i) {
                const auto& row = layout.at("rows").at(i);
                const int y = top + row.at("top").get<int>();
                if (input_.mouse_y >= y && input_.mouse_y < y + row.at("height").get<int>()) {
                    const int index = row.value("index", static_cast<int>(i));
                    if (!choice_.at("strings").at(index).get_ref<const std::string&>().empty()) { selected = index; hit = true; }
                    break;
                }
            }
        }
        if (input_.mouse_left_new && !hit && !keyboard_confirm) done = false;
    }
    if (done) choice_sound(9000017);
    const int cancel = choice_.at("cancel");
    if (((input_.cancel && !previous_input_.cancel) || input_.mouse_right_new) && cancel != 1) {
        selected = cancel == 0 ? -2 : cancel - 2; done = true; choice_sound(9000019);
    }
    choice_["selected"] = selected; choice_["done"] = done; set_number(9000020, selected);
}

void Runtime::choice_sound(int variable) {
    const int id = number(variable);
    if (id < 0) return;
    const auto& row = databases_.at("system").at("types").at(3).at("rows").at(id);
    const std::string file = row.at(0);
    if (file.empty()) return;
    const int volume = row.at(1), frequency = row.at(2);
    audio_.push_back({{"operation", "play"}, {"kind", 2}, {"file", expand(file, {})}, {"channel", 0},
        {"volume", volume == 0 ? 100 : volume}, {"tempo", frequency == 0 ? 100 : frequency},
        {"fade_ms", 0}, {"start_ms", 0}, {"loop_ms", 0}});
}

std::array<double, 3> picture_group_zoom(const Json& picture, std::uint64_t frame) {
    if (!picture.contains("zoom_effect")) return {0, 0, 1};
    const auto& effect = picture.at("zoom_effect");
    const auto start = effect.at("start").get<std::uint64_t>();
    const auto duration = effect.at("duration").get<std::uint64_t>();
    const double fraction = duration ? static_cast<double>(std::min(frame >= start ? frame - start : 0, duration)) / duration : 1;
    const double from = effect.at("from");
    return {effect.at("x"), effect.at("y"), (from + (effect.at("percent").get<double>() - from) * fraction) / 100};
}

std::array<double, 2> picture_automatic_zoom(const Json& picture, std::uint64_t frame) {
    if (!picture.contains("automatic_zoom")) return {1,1};
    const auto& effect=picture.at("automatic_zoom");
    const auto start=effect.at("start").get<std::uint64_t>(),duration=effect.at("duration").get<std::uint64_t>();
    if (!duration || frame<start) return {1,1};
    const auto phase=(frame-start+1)%(duration*2+effect.at("pause").get<std::uint64_t>());
    // Supplied Windows engine: linear expansion/return, followed by a rest.
    const double amount=phase<duration ? static_cast<double>(phase)/duration : phase<duration*2 ? 2-static_cast<double>(phase)/duration : 0;
    return {1+effect.at("x").get<double>()*amount/100,1+effect.at("y").get<double>()*amount/100};
}

std::array<double, 3> picture_tint(const Json& picture, std::uint64_t frame) {
    std::array<double, 3> result{picture.value("red", 100.0), picture.value("green", 100.0), picture.value("blue", 100.0)};
    if (picture.contains("color_correction")) {
        const auto& effect = picture.at("color_correction");
        const auto start = effect.at("start").get<std::uint64_t>(), duration = effect.at("duration").get<std::uint64_t>();
        const double fraction = duration ? static_cast<double>(std::min(frame >= start ? frame - start : 0, duration)) / duration : 1;
        for (int i = 0; i < 3; ++i) result[i] += effect.at("from").at(i).get<double>() +
            (effect.at("to").at(i).get<double>() - effect.at("from").at(i).get<double>()) * fraction;
    }
    if (picture.contains("flash")) {
        const auto& effect = picture.at("flash");
        const auto start = effect.at("start").get<std::uint64_t>(), duration = effect.at("duration").get<std::uint64_t>();
        const double amount = duration ? 1 - static_cast<double>(std::min(frame >= start ? frame - start : 0, duration)) / duration : 0;
        for (int i = 0; i < 3; ++i) result[i] += effect.at("color").at(i).get<double>() * amount;
    }
    for (auto& channel : result) channel = std::max(0.0, channel);
    if (!picture.contains("flicker")) return result;
    const auto& flicker = picture.at("flicker");
    const auto start = flicker.at("start").get<std::uint64_t>();
    const auto duration = flicker.at("duration").get<std::uint64_t>();
    if (duration == 0 || frame < start) return result;
    const double phase = static_cast<double>((frame - start) % (duration * 2)) / duration;
    const double amount = flicker.at("type") == 5 ? (phase <= 1 ? phase : 2 - phase) :
        1 - static_cast<double>((frame - start) % duration) / duration;
    for (int i = 0; i < 3; ++i) result[i] = std::clamp(result[i] + flicker.at("color").at(i).get<double>() * amount, 0.0, 200.0);
    return result;
}

std::pair<std::int64_t, std::int64_t> picture_draw_offset(const Json& picture, std::uint64_t frame) {
    std::pair<std::int64_t, std::int64_t> result{0, 0};
    if (picture.contains("draw_shift")) result = {picture.at("draw_shift").at("x"), picture.at("draw_shift").at("y")};
    if (!picture.contains("shake")) return result;
    const auto& shake = picture.at("shake");
    const auto start = shake.at("start").get<std::uint64_t>();
    if (frame < start) return result;
    const auto duration = shake.at("duration").get<std::uint64_t>();
    // Reference samples include the initial two internal effect updates. Each
    // repetition is one excursion and return; successive excursions alternate.
    const auto age = frame - start + 2;
    if (age >= duration * 2 * shake.at("count").get<std::uint64_t>()) return result;
    const auto quarter = (age / duration) % 4, step = age % duration;
    auto offset = [&](const char* field) {
        const auto amplitude = shake.at(field).get<std::int64_t>();
        const std::array<std::int64_t, 5> points{0, amplitude, 0, -amplitude, 0};
        return points[quarter] + (points[quarter + 1] - points[quarter]) * static_cast<std::int64_t>(step) / static_cast<std::int64_t>(duration);
    };
    result.first += offset("x"); result.second += offset("y"); return result;
}

int picture_pattern(const Json& picture, std::uint64_t frame) {
    if (!picture.contains("pattern_effect")) return picture.at("pattern");
    const auto& effect = picture.at("pattern_effect");
    const int first = effect.at("first"), last = effect.at("last");
    const auto age = frame - effect.at("start").get<std::uint64_t>();
    const auto elapsed = age / effect.at("interval").get<std::uint64_t>();
    const int count = std::abs(last - first) + 1, type = effect.at("type");
    if (type == 8 && elapsed >= static_cast<unsigned>(count)) return last;
    int step = static_cast<int>(elapsed % count);
    if (type == 10 && count > 1) {
        step = static_cast<int>(elapsed % (2 * count - 2)); if (step >= count) step = 2 * count - 2 - step;
    }
    return first + (first <= last ? step : -step);
}

void Runtime::execute(Run& run, const Json& cmd) {
    auto& f = run.stack.back(); const auto context = f.context;
    const int code = cmd.at("code"), indent = cmd.at("indent");
    auto value = [&](std::size_t i) { return number(arg(cmd, i), context); };
    switch (code) {
    case 0: case 99: case 103: case 212: case 499: return;
    case 101: show_message(run, cmd); return;
    case 104:
        // This operand-free legacy command is inert in the supplied 3.191
        // executable: it preserves pictures/effects and an active message.
        // In particular, the source comment "Effects off" is not its behavior.
        if (!cmd.at("args").empty() || !cmd.at("strings").empty()) fail("Unsupported legacy command 104 layout");
        return;
    case 106:
        if (!cmd.at("args").empty() || cmd.at("strings").size() != 1) fail("Unsupported debug-text layout");
        if (debug_text_.size() == 64) debug_text_.erase(debug_text_.begin());
        debug_text_.push_back(expand(cmd.at("strings").at(0), context));
        return;
    case 107:
        if (!cmd.at("args").empty() || !cmd.at("strings").empty()) fail("Unsupported clear-debug-text layout");
        debug_text_.clear();
        return;
    case 105:
        if (!cmd.at("args").empty() || !cmd.at("strings").empty()) fail("Unsupported force-message-stop layout");
        if (!message_state_.empty()) {
            message_state_["done"] = true;
            message_state_["visible"] = 0;
            message_.clear();
        }
        set_number(9000012, 0);
        return;
    case 102: show_choice(run, cmd); return;
    case 130: transfer(cmd, context); return;
    case 111: {
        std::vector<bool> conditions;
        const int count = arg(cmd, 0) & 15;
        if ((arg(cmd, 0) & ~31) || count == 0 || cmd.at("args").size() != static_cast<std::size_t>(1 + count * 3)) fail("Unsupported numeric-condition layout");
        for (int i = 0; i < count; ++i) {
            const int options = arg(cmd, 3 + 3 * i);
            if (options & ~31) fail("Unsupported numeric-condition flags");
            const int right = options & 16 ? arg(cmd, 2 + 3 * i) : value(2 + 3 * i);
            conditions.push_back(compare(value(1 + 3 * i), right, options & 15));
        }
        branch(f, indent, conditions); return;
    }
    case 112: {
        const int count = arg(cmd, 0) & 15;
        if (count < 1 || count > 8 || (arg(cmd, 0) & ~31)) fail("Unsupported string-condition layout");
        std::size_t next_value = 1 + count;
        std::vector<bool> conditions;
        for (int i = 0; i < count; ++i) {
            const auto packed = static_cast<std::uint32_t>(arg(cmd, 1 + i));
            const int options = packed >> 24;
            if (options & 14) fail("Unsupported string-condition flags");
            const auto left = text(packed & 0xffffff, context);
            const auto right = options & 1 ? text(arg(cmd, next_value++), context) : cmd.at("strings").at(i).get<std::string>();
            switch (options >> 4) {
            case 0: conditions.push_back(left == right); break;
            case 1: conditions.push_back(left != right); break;
            case 2: conditions.push_back(left.find(right) != std::string::npos); break;
            case 3: conditions.push_back(left.compare(0, right.size(), right) == 0); break;
            default: fail("Unsupported string comparison operator");
            }
        }
        if (next_value != cmd.at("args").size()) fail("Unexpected string-condition operands");
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
        if (options & 65536) {
            if (cmd.at("args").size() != 5 || (options & ~0x1ff7f) || (options & 16)) fail("Unsupported arithmetic variable-range layout");
            const int distance = arg(cmd, 4), first = arg(cmd, 0);
            if (distance < 0 || distance > 65535 || static_cast<std::int64_t>(first) + distance > std::numeric_limits<int>::max())
                fail("Unsupported arithmetic variable-range extent");
            Json single = cmd;
            single["args"] = Json::array({first, arg(cmd, 1), arg(cmd, 2), options & 65535});
            // Each iteration reads operands again: an earlier destination may
            // also be a source, and random ranges draw once per variable.
            for (int offset = 0; offset <= distance; ++offset) {
                single["args"][0] = first + offset; execute(run, single);
            }
            return;
        }
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
    case 122: string_operation(cmd, context); return;
    case 140: {
        const int header = arg(cmd, 0), kind = (header >> 4) & 15, process = header & 15;
        const auto count = cmd.at("args").size();
        auto stop = [&](int channel, int fade) {
            if (kind == 2) for (auto it = pending_audio_.begin(); it != pending_audio_.end();) {
                const auto& event = it->at("event");
                if (event.at("kind") == 2 && (channel == -1 || event.at("channel") == channel)) it = pending_audio_.erase(it);
                else ++it;
            }
            audio_.push_back({{"operation", "stop"}, {"kind", kind}, {"channel", channel}, {"fade_ms", fade}});
            if (kind < 2) music_.erase(std::to_string(kind));
        };
        // Older game data also sign-extends the -1 database stop sentinel.
        if (kind <= 2 && (header == (0x00ffff00 | (kind << 4)) || header == (-256 | (kind << 4)))) {
            if ((count != 4 && !(kind == 2 && count == 7)) || !cmd.at("strings").empty() || value(2) != 0 ||
                (kind < 2 && value(3) != 0) || (count == 7 && (arg(cmd, 4) || arg(cmd, 5) || arg(cmd, 6))))
                fail("Unsupported sound-stop layout");
            stop(kind == 2 ? value(3) : -1, kind == 2 ? 0 : std::max(0, value(1)) * 1000 / project_->at("game").at("fps").get<int>());
            return;
        }
        if (kind > 2 || (process != 0 && process != 1 && process != 3)) fail("Unsupported sound operation");
        const bool filename = (header & 0xff000000) == 0x02000000;
        std::string file; int volume = 100, frequency = 100, loop = 0;
        if (filename) {
            const bool extended_se = kind == 2 && count == 7 && arg(cmd, 6) == 0;
            if (header != (0x02000000 | (kind << 4) | process) ||
                (count != (kind == 2 ? 6u : 7u) && !extended_se) || cmd.at("strings").size() != 1 || value(2) != 0)
                fail("Unsupported filename sound-command layout");
            file = expand(cmd.at("strings").at(0), context);
            volume = value(4); frequency = value(5); loop = kind == 2 ? 0 : value(6);
        } else {
            const bool variable = header == (0x01000000 | (kind << 4));
            const bool direct = (header & 0xff00000f) == 0;
            if ((!variable && !direct) || count != 4 || !cmd.at("strings").empty()) fail("Unsupported database sound-command layout");
            int entry = variable ? value(2) : (header >> 8) & 65535;
            if (!variable && entry >= 32768) entry -= 65536;
            if (entry == -1) { stop(kind == 2 ? value(3) : -1, kind == 2 ? 0 : std::max(0, value(1)) * 1000 / project_->at("game").at("fps").get<int>()); return; }
            const auto& rows = databases_.at("system").at("types").at(1 + kind).at("rows");
            if (entry < 0 || entry >= static_cast<int>(rows.size())) fail("Missing sound database entry");
            const auto& row = rows.at(entry);
            file = expand(row.at(0), context); volume = row.at(1); frequency = row.at(2);
            if (volume == 0) volume = 100;
            if (row.size() > 3) loop = row.at(3);
        }
        if (file.empty() && process != 3) {
            if (process == 0 && kind < 2) stop(-1, std::max(0, value(1)) * 1000 / project_->at("game").at("fps").get<int>());
            return;
        }
        if (file.find_first_of("\r\n") != std::string::npos) {
            std::istringstream stream(file); std::vector<std::string> lines; std::string line;
            while (std::getline(stream, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); lines.push_back(line); }
            if (lines.size() < 3 || lines.size() > (kind == 2 ? 3u : 4u)) fail("Unsupported inline sound metadata");
            auto integer = [&](std::size_t index) {
                std::size_t used = 0; const int result = std::stoi(lines.at(index), &used);
                if (used != lines.at(index).size()) fail("Invalid inline sound metadata");
                return result;
            };
            file = lines.at(0); volume = integer(1); frequency = integer(2);
            if (lines.size() == 4) loop = integer(3);
        }
        if (frequency < 0) fail("Negative sound frequency is not supported");
        if (frequency == 0) frequency = 100;
        const Json event = {{"operation", process == 1 ? "preload" : process == 3 ? "release_unused" : "play"}, {"kind", kind}, {"file", file},
            {"channel", kind == 2 ? value(3) : 0}, {"volume", volume}, {"tempo", frequency},
            {"start_ms", kind == 2 ? 0 : std::max(0, value(3))}, {"loop_ms", loop},
            {"fade_ms", kind == 2 ? 0 : std::max(0, value(1)) * 1000 / project_->at("game").at("fps").get<int>()}};
        const int delay = kind == 2 && process == 0 ? std::max(0, value(1)) : 0;
        if (delay) pending_audio_.push_back({{"start", frame_ + delay}, {"event", event}});
        else audio_.push_back(event);
        if (kind < 2 && process == 0) remember_music(event);
        if (kind < 2 && process == 0) { set_number(9000095 + kind * 2, volume); set_number(9000096 + kind * 2, frequency); }
        return;
    }
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
        } else if (kind == 1 || kind == 2) {
            // WOLF keyboard codes are its own layout: A=130, B=148.
            // Bit 0 restricts the query to the key in the third operand.
            const int specific = (options & 1) ? number(arg(cmd, 2), context) : -1;
            for (int code : input_.keyboard) {
                if ((kind == 1 && code >= 300) || (kind == 2 && code < 300)) continue;
                if (specific < 0 || specific == code) { pressed = code; break; }
            }
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
    case 220: game_save_load(run, cmd); return;
    case 174:
        if (!cmd.at("args").empty() || !cmd.at("strings").empty()) fail("Unsupported return-to-title layout");
        pending_title_reset_ = true;
        return;
    case 1000: {
        const int operation = arg(cmd, 0);
        if (cmd.at("args").size() != 1 || cmd.at("strings").size() != 1 || (operation != 0 && operation != 3 && operation != 4))
            fail("Unsupported screenshot/file-operation layout");
        if (save_root_.empty()) fail("Game file operations require persistent native storage");
        file_operations_.push_back({{"operation", operation}, {"path", expand(cmd.at("strings").at(0), context)}});
        run.wake = frame_ + 1;
        return;
    }
    case 160:
        if (cmd.at("args").size() != 2) fail("Unexpected transition configuration layout");
        transition_ = {{"id", value(0)}, {"frames", arg(cmd, 1) & 65535},
            {"wait", ((arg(cmd, 1) >> 16) & 255) != 0}};
        return;
    case 124: {
        if (((arg(cmd, 1) >> 12) & 15) == 4) { picture_query(cmd, context); return; }
        if (cmd.at("args").size() != 3 || ((arg(cmd, 1) >> 12) & 15) != 3 || (arg(cmd, 1) & 255))
            fail("Unsupported extended-variable query layout");
        int result = 0;
        switch (arg(cmd, 2)) {
        case 0: result = map_; break;
        case 7: result = input_.mouse_left_new ? 2 : input_.mouse_left ? 1 : 0; break;
        case 8: result = input_.mouse_right_new ? 2 : input_.mouse_right ? 1 : 0; break;
        case 9: result = input_.mouse_middle && !previous_input_.mouse_middle ? 2 : input_.mouse_middle ? 1 : 0; break;
        case 10: result = input_.wheel; break;
        case 15: result = current_map()->at("width"); break;
        case 16: result = current_map()->at("height"); break;
        case 17: result = context.common; break;
        case 18:
            result = -1;
            for (const auto& active : runs_) if (active.blocking && !active.stack.empty()) {
                const auto root = active.stack.front().context;
                result = root.common >= 0 ? 500000 + root.common : root.event; break;
            }
            break;
        case 20: result = input_.mouse_x; break;
        case 21: result = input_.mouse_y; break;
        default: fail("Unsupported extended-variable query field " + std::to_string(arg(cmd, 2)));
        }
        set_number(arg(cmd, 0), assign(number(arg(cmd, 0), context), result, (arg(cmd, 1) >> 8) & 15), context);
        return;
    }
    case 125: {
        const int options = arg(cmd, 0);
        if (cmd.at("args").size() != 1 || !cmd.at("strings").empty()) fail("Unsupported automatic-input operands");
        if ((options & ~247) == 0) pending_auto_input_ |= options;
        else if ((options & ~7) == 0x20000000) pending_auto_mouse_input_ |= options & 7;
        else fail("Unsupported automatic-input mode or mouse movement operands");
        return;
    }
    case 126: {
        if (cmd.at("args").size() != 1) fail("Unsupported input-permission operand count");
        const int options = arg(cmd, 0);
        if ((options & 0xff000000) == 0x10000000) {
            if (options != 0x10000001 && options != 0x10000101) fail("Only mouse-device permissions are implemented");
            mouse_enabled_ = (options & 256) == 0;
        } else {
            const int mode = (options >> 8) & 255;
            if ((options & ~0x3f7) || mode > 2) fail("Unsupported basic-input permissions");
            const std::array<int, 7> bits{1, 2, 4, 16, 32, 64, 128};
            for (std::size_t i = 0; i < bits.size(); ++i) if (options & bits[i]) input_permissions_[i] = mode;
        }
        apply_input_permissions(); return;
    }
    case 250: {
        const int options = arg(cmd, 3), bank = (options >> 8) & 15, operation = (options >> 12) & 15;
        if ((options & 15) || bank > 2 || operation > 1) fail("Unsupported database operation mode");
        const char* names[] = {"mutable", "system", "user"};
        int type_id = value(0), row_id = value(1), field_id = value(2);
        const int named = (options >> 16) & 7;
        const auto& original_types = project_->at("databases").at(names[bank]).at("types");
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
            if ((candidate == 170 || candidate == 179) &&
                (code == 498 ? depth == indent : depth < indent && f.loops.count(start) != 0)) { found = true; break; }
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
    case 290: picture_effect(cmd, context); return;
    case 213: {
        const auto& list = commands(context); const auto label = expand(cmd.at("strings").at(0), context);
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list.at(i).at("code") == 212 && list.at(i).at("strings").at(0) == label) { f.pc = i + 1; return; }
        // Game.exe falls through when the target is absent. This game's mod
        // contains such a jump before the evening interaction checks.
        if (warnings_.size() < 32) warnings_.push_back({{"kind", "missing_label"}, {"label", label},
            {"common", context.common}, {"event", context.event}, {"offset", cmd.at("offset")}});
        return;
    }
    default: fail("Unsupported command " + std::to_string(code) + " at original byte offset " + std::to_string(cmd.at("offset").get<int>()));
    }
}

Json Runtime::take_audio() { Json result = std::move(audio_); audio_ = Json::array(); return result; }
Json Runtime::take_file_operations() {
    Json result = std::move(file_operations_); file_operations_ = Json::array();
    if (!result.empty()) resources_->invalidate();
    return result;
}

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
    Json message = Json::object();
    if (!message_state_.empty()) for (const char* field : {"x", "y", "visible", "total", "complete", "done"}) message[field] = message_state_.at(field);
    return {{"frame", frame_}, {"map", map_}, {"error", error_}, {"project_id", project_id_}, {"message", message},
        {"context", {{"common", error_context_.common}, {"event", error_context_.event}, {"page", error_context_.page}}},
        {"command", error_command_}, {"executed", counts}, {"pictures", pictures_.size()}, {"stacks", stacks}, {"warnings", warnings_}, {"debug_text", debug_text_}};
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
    return {{"format", "wolf-ios-state"}, {"version", 6}, {"title", project_->at("game").at("title")}, {"project_id", project_id_},
        {"frame", frame_}, {"play_time_ticks", play_time_ticks_}, {"map", map_}, {"hero_x", hero_x_}, {"hero_y", hero_y_},
        {"numbers", numbers}, {"strings", strings}, {"databases", databases_}, {"pictures", pictures_},
        {"runs", runs}, {"rng", rng.str()}, {"message", message_}, {"transition", transition_},
        {"characters", characters_}, {"party", party_}, {"routes", routes}, {"next_route", next_route_},
        {"pending_picture_erases", pending_picture_erases_}, {"pending_picture_changes", pending_picture_changes_}, {"choice", choice_}, {"choice_clear_after", choice_clear_after_}, {"message_state", message_state_}, {"music", music_},
        {"input_permissions", input_permissions_}, {"mouse_enabled", mouse_enabled_}, {"pending_auto_input", pending_auto_input_},
        {"pending_auto_mouse_input", pending_auto_mouse_input_}, {"pending_audio", pending_audio_},
        {"previous_input", {{"up", previous_input_.up}, {"down", previous_input_.down}, {"left", previous_input_.left}, {"right", previous_input_.right},
            {"confirm", previous_input_.confirm}, {"cancel", previous_input_.cancel}, {"sub", previous_input_.sub},
            {"mouse_left", previous_input_.mouse_left}, {"mouse_right", previous_input_.mouse_right}, {"mouse_middle", previous_input_.mouse_middle},
            {"mouse_x", previous_input_.mouse_x}, {"mouse_y", previous_input_.mouse_y}, {"keyboard", previous_input_.keyboard}}}};
}

void Runtime::restore(const Json& state) {
    if (state.at("format") != "wolf-ios-state" || state.at("version") != 6 ||
        state.at("title") != project_->at("game").at("title") || state.at("project_id") != project_id_)
        fail("Incompatible native save: source game data differs");
    Runtime candidate(project_, save_root_, asset_root_, resource_root_, project_id_);
    candidate.frame_ = state.at("frame"); candidate.map_ = state.at("map");
    candidate.play_time_ticks_ = state.at("play_time_ticks");
    candidate.hero_x_ = state.at("hero_x"); candidate.hero_y_ = state.at("hero_y");
    candidate.numbers_.clear();
    for (const auto& [key, value] : state.at("numbers").items()) candidate.numbers_[std::stoull(key)] = value;
    for (const auto& [key, value] : state.at("strings").items()) candidate.strings_[std::stoull(key)] = value;
    candidate.databases_ = state.at("databases"); candidate.pictures_ = state.at("pictures");
    candidate.transition_ = state.at("transition");
    candidate.choice_ = state.at("choice"); candidate.choice_clear_after_ = state.at("choice_clear_after");
    candidate.message_state_ = state.at("message_state");
    candidate.input_permissions_ = state.at("input_permissions").get<std::array<int, 7>>();
    candidate.mouse_enabled_ = state.at("mouse_enabled");
    candidate.pending_auto_input_ = state.at("pending_auto_input");
    candidate.pending_auto_mouse_input_ = state.value("pending_auto_mouse_input", 0);
    candidate.pending_audio_ = state.value("pending_audio", Json::array());
    candidate.music_ = state.value("music", Json::object());
    candidate.resume_music();
    if (!candidate.pending_audio_.is_array()) fail("Invalid saved delayed sound queue");
    for (const auto& pending : candidate.pending_audio_) {
        (void)pending.at("start").get<std::uint64_t>();
        if (pending.at("event").at("kind") != 2 || pending.at("event").at("operation") != "play") fail("Invalid saved delayed sound event");
    }
    if (candidate.pending_auto_input_ & ~247) fail("Invalid saved automatic input");
    if (candidate.pending_auto_mouse_input_ & ~7) fail("Invalid saved automatic mouse input");
    for (int mode : candidate.input_permissions_) if (mode < 0 || mode > 2) fail("Invalid saved input permissions");
    if (!candidate.message_state_.empty()) {
        const int visible = candidate.message_state_.at("visible"), total = candidate.message_state_.at("total");
        if (total < 0 || visible < 0 || visible > total) fail("Invalid saved message progress");
    }
    if (!candidate.choice_.empty()) {
        const int selected = candidate.choice_.at("selected");
        if (!candidate.choice_.at("strings").is_array() || selected < -2 || selected >= static_cast<int>(candidate.choice_.at("strings").size()))
            fail("Invalid saved choice state");
    }
    const auto& previous_input = state.at("previous_input");
    candidate.previous_input_.up = previous_input.at("up"); candidate.previous_input_.down = previous_input.at("down");
    candidate.previous_input_.confirm = previous_input.at("confirm"); candidate.previous_input_.cancel = previous_input.at("cancel");
    candidate.previous_input_.left = previous_input.at("left"); candidate.previous_input_.right = previous_input.at("right"); candidate.previous_input_.sub = previous_input.at("sub");
    candidate.previous_input_.mouse_left = previous_input.at("mouse_left"); candidate.previous_input_.mouse_right = previous_input.at("mouse_right"); candidate.previous_input_.mouse_middle = previous_input.at("mouse_middle");
    candidate.previous_input_.mouse_x = previous_input.at("mouse_x"); candidate.previous_input_.mouse_y = previous_input.at("mouse_y");
    candidate.previous_input_.keyboard = previous_input.value("keyboard", std::set<int>{});
    for (int key : candidate.previous_input_.keyboard) if (key < 100 || key > 399) fail("Invalid saved keyboard key");
    candidate.pending_picture_erases_ = state.at("pending_picture_erases");
    if (!candidate.pending_picture_erases_.is_array()) fail("Invalid delayed-picture state");
    for (const auto& erase : candidate.pending_picture_erases_) {
        (void)erase.at("id").get<int>(); (void)erase.at("start").get<std::uint64_t>();
        if (erase.at("duration").get<int>() < 0) fail("Invalid delayed-picture duration");
    }
    candidate.pending_picture_changes_ = state.at("pending_picture_changes");
    if (!candidate.pending_picture_changes_.is_array()) fail("Invalid delayed-picture changes");
    for (const auto& pending : candidate.pending_picture_changes_) {
        (void)pending.at("start").get<std::uint64_t>();
        const auto& action = pending.at("action");
        (void)action.at("id").get<int>();
        if ((action.at("operation") != 0 && action.at("operation") != 1) || action.at("duration").get<int>() < 0 || !action.at("values").is_object())
            fail("Invalid delayed-picture action");
    }
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
    candidate.set_number_freezes(number_freezes_);
    *this = std::move(candidate);
}
}
