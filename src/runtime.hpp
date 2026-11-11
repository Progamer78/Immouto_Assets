#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <atomic>
#include <set>
#include <memory>
#include <array>
#include "resources.hpp"

class TextRasterizer;

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
    std::set<int> keyboard;
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
    std::uint64_t waiting_route = 0;
    bool waiting_all_routes = false;
    std::vector<Frame> stack;
};
struct Route {
    std::uint64_t id = 0, wake = 0;
    std::size_t pc = 0;
    std::string target;
    Context context;
    Json source;
};

class Runtime {
public:
    explicit Runtime(Json project, std::string save_root = {}, std::string asset_root = {}, std::string resource_root = {});
    void tick(const Input& input = {});
    void start_common(int id);
    std::int32_t number(int ref, Context context = {});
    void set_number(int ref, std::int32_t value, Context context = {});
    void set_number_freezes(const std::map<int, std::int32_t>& values);
    std::vector<int> numeric_variable_refs() const;
    std::string text(int ref, Context context = {});
    void set_text(int ref, std::string value, Context context = {});
    std::string expand(std::string value, Context context);
    Json snapshot() const;
    void restore(const Json& state);
    void return_to_title();
    // Imports persistent PC progress into a vacant normal slot, preserving the
    // live session. Loading it runs the game's original post-load processing.
    int import_pc_progress(const Json& progress);
    void refresh_resources() { resources_->invalidate(); }
    Json diagnostics() const;

    const Json& project() const { return *project_; }
    const Json& pictures() const { return pictures_; }
    const Json& characters() const { return characters_; }
    const Json& choice() const { return choice_; }
    const Json& message_state() const { return message_state_; }
    const std::string& error() const { return error_; }
    const std::string& message() const { return message_; }
    std::uint64_t frame() const { return frame_; }
    const Json* current_map() const;
    bool quit_requested() const { return quit_; }
    Json take_audio();
    Json take_file_operations();
    bool file_operations_pending() const { return !file_operations_.empty(); }
    Json choice_pictures() const;

private:
    Runtime(std::shared_ptr<const Json> project, std::string save_root, std::string asset_root, std::string resource_root, std::string identity);
    std::shared_ptr<const Json> project_;
    std::map<int, std::int32_t> number_freezes_; // External app tools; never part of original game saves.
    Json databases_, pictures_ = Json::object(), audio_ = Json::array();
    Json transition_ = {{"id", -1}, {"frames", 0}, {"wait", false}};
    Json pending_picture_erases_ = Json::array();
    Json pending_picture_changes_ = Json::array();
    Json pending_audio_ = Json::array();
    Json music_ = Json::object();
    Json pending_game_load_ = Json::object();
    bool pending_title_reset_ = false;
    Json file_operations_ = Json::array();
    Json choice_ = Json::object();
    Json message_state_ = Json::object();
    std::uint64_t choice_clear_after_ = 0;
    Json characters_ = Json::object();
    Json party_ = {{"images", {"", "", "", "", "", ""}}, {"visible", true}};
    std::vector<Route> routes_;
    std::uint64_t next_route_ = 1;
    std::map<std::uint64_t, std::int32_t> numbers_;
    std::map<std::uint64_t, std::string> strings_;
    std::vector<Run> runs_;
    std::mt19937 random_{0};
    std::map<int, std::uint64_t> executed_;
    std::uint64_t frame_ = 0;
    std::uint64_t play_time_ticks_ = 0;
    int map_ = 0, hero_x_ = 0, hero_y_ = 0;
    bool quit_ = false;
    Input input_;
    Input raw_input_;
    Input previous_input_;
    std::array<int, 7> input_permissions_{};
    bool mouse_enabled_ = true;
    int pending_auto_input_ = 0;
    std::string error_, message_;
    Json warnings_ = Json::array();
    Json debug_text_ = Json::array();
    std::string project_id_;
    std::string save_root_;
    std::string asset_root_;
    std::string resource_root_;
    std::shared_ptr<Resources> resources_;
    std::shared_ptr<TextRasterizer> text_rasterizer_;
    std::map<std::string, Json> save_slots_;
    std::set<std::string> dirty_saves_;
    Context error_context_;
    Json error_command_;

    std::uint64_t key(int ref, Context context) const;
    Json& cell(int ref);
    const Json& common(int id) const;
    const Json* map_data(int id) const;
    const Json& commands(Context context) const;
    bool compare(int left, int right, int operation) const;
    bool page_active(const Json& page, Context context);
    void schedule();
    void advance(Run& run);
    void execute(Run& run, const Json& command);
    void branch(Frame& frame, int indent, const std::vector<bool>& conditions);
    void picture(const Json& command, Context context);
    void move_picture(const Json& command, Context context);
    void apply_picture_change(const Json& action);
    void advance_pictures();
    void erase_picture(int id, int duration);
    void erase_picture_range(int first, int last, int duration);
    void picture_effect(const Json& command, Context context);
    void picture_query(const Json& command, Context context);
    void show_choice(Run& run, const Json& command);
    void prepare_choice_layout();
    void advance_choice();
    void show_message(Run& run, const Json& command);
    void advance_message();
    void apply_input_permissions();
    void transfer(const Json& command, Context context);
    void choice_sound(int system_variable);
    void string_operation(const Json& command, Context context);
    void call(Run& run, const Json& command);
    std::string character_key(int target, Context context);
    void start_route(Run& run, const Json& command);
    void advance_route(Route& route);
    void party(const Json& command, Context context);
    bool route_pending(const Run& run) const;
    bool string_reference(int ref);
    void saved_variable(const Json& command, Context context);
    std::string save_name(int identifier, Context context);
    Json& save_slot(const std::string& name);
    void game_save_load(Run& run, const Json& command);
    void apply_game_load();
    void reset_to_title();
    void remember_music(const Json& event);
    void resume_music();
    void flush_saves();
    std::int32_t assign(std::int32_t current, std::int64_t value, int operation) const;
    void fail(const std::string& message) const;
};

Json load_json(const std::string& filename);
void atomic_save(const std::string& filename, const Json& state);
void atomic_write(const std::string& filename, const std::string& bytes);
std::array<double, 3> picture_tint(const Json& picture, std::uint64_t frame);
std::array<double, 3> picture_group_zoom(const Json& picture, std::uint64_t frame);
std::array<double, 2> picture_automatic_zoom(const Json& picture, std::uint64_t frame);
int picture_pattern(const Json& picture, std::uint64_t frame);
std::pair<std::int64_t, std::int64_t> picture_draw_offset(const Json& picture, std::uint64_t frame);
}
