#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <atomic>

namespace wolf {
using Json = nlohmann::json;
struct CrashBreadcrumb {
    std::atomic<int> code{-1}, common{-1}, map{-1}, event{-1}, offset{-1};
};
extern CrashBreadcrumb crash_breadcrumb;

struct Context { int common = -1, event = -1, page = -1, map = 0; };
struct Input {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, cancel = false, sub = false;
    bool mouse_left = false, mouse_right = false, mouse_middle = false;
    bool mouse_left_new = false, mouse_right_new = false;
    int mouse_x = 0, mouse_y = 0, wheel = 0;
};
struct Frame {
    Context context;
    std::size_t pc = 0;
    int return_target = -1;
    std::map<std::size_t, int> loops;
};
struct Run {
    std::string owner;
    bool blocking = false;
    std::uint64_t wake = 0;
    std::vector<Frame> stack;
};

class Runtime {
public:
    explicit Runtime(Json project);
    void tick(const Input& input = {});
    void start_common(int id);
    std::int32_t number(int ref, Context context = {});
    void set_number(int ref, std::int32_t value, Context context = {});
    std::string text(int ref, Context context = {});
    void set_text(int ref, std::string value, Context context = {});
    std::string expand(std::string value, Context context);
    Json snapshot() const;
    void restore(const Json& state);
    Json diagnostics() const;

    const Json& project() const { return project_; }
    const Json& pictures() const { return pictures_; }
    const std::string& error() const { return error_; }
    const std::string& message() const { return message_; }
    std::uint64_t frame() const { return frame_; }
    const Json* current_map() const;
    bool quit_requested() const { return quit_; }
    Json take_audio();

private:
    Json project_, databases_, pictures_ = Json::object(), audio_ = Json::array();
    Json transition_ = {{"id", -1}, {"frames", 0}, {"wait", false}};
    std::map<std::uint64_t, std::int32_t> numbers_;
    std::map<std::uint64_t, std::string> strings_;
    std::vector<Run> runs_;
    std::mt19937 random_{0};
    std::map<int, std::uint64_t> executed_;
    std::uint64_t frame_ = 0;
    int map_ = 0, hero_x_ = 0, hero_y_ = 0;
    bool quit_ = false;
    Input input_;
    std::string error_, message_;
    std::string project_id_;
    Context error_context_;
    Json error_command_;

    std::uint64_t key(int ref, Context context) const;
    Json& cell(int ref);
    const Json& common(int id) const;
    const Json& commands(Context context) const;
    bool compare(int left, int right, int operation) const;
    bool page_active(const Json& page, Context context);
    void schedule();
    void advance(Run& run);
    void execute(Run& run, const Json& command);
    void branch(Frame& frame, int indent, const std::vector<bool>& conditions);
    void picture(const Json& command, Context context);
    void call(Run& run, const Json& command);
    std::int32_t assign(std::int32_t current, std::int64_t value, int operation) const;
    void fail(const std::string& message) const;
};

Json load_json(const std::string& filename);
void atomic_save(const std::string& filename, const Json& state);
}
