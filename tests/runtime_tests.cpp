#include "runtime.hpp"
#include "asset_cache.hpp"
#include "input_replay.hpp"
#include "text.hpp"
#include "shapes.hpp"
#include "images.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using wolf::Json;
#define REQUIRE(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (false)

Json cmd(int code, Json args = Json::array(), int indent = 0, Json strings = Json::array()) {
    return {{"code", code}, {"args", args}, {"indent", indent}, {"strings", strings}, {"offset", 0}};
}
Json project(const std::vector<Json>& commands) {
    Json types = Json::array();
    for (int i = 0; i < 8; ++i) types.push_back({{"rows", Json::array()}});
    types[0]["rows"] = {{"MapData/Test.mps"}}; types[7]["rows"] = {{0, 0, 0}};
    types[3]["rows"] = Json::array({Json::array({"", 100, 100, 0})});
    Json p = {{"format", "wolf-ios-ir"}, {"version", 1}};
    p["game"] = {{"title", "fixture"}, {"width", 1024}, {"height", 576}, {"fps", 60}};
    p["common"]["events"] = Json::array({{{"id", 0}, {"name", "test"}, {"mode", 0},
        {"return_id", 1600000}, {"commands", Json(commands)}}});
    p["databases"]["system"]["types"] = types;
    p["databases"]["mutable"]["types"] = Json::array({{{"rows", {{42, "original"}}}}});
    p["databases"]["user"]["types"] = Json::array();
    p["maps"] = Json::array({{{"file", "MapData/Test.mps"}, {"events", Json::array()}}});
    return p;
}

void execution_and_waits() {
    wolf::Runtime rt(project({cmd(121, {2000000, 5, 0, 0}), cmd(180, {2}), cmd(121, {2000000, 3, 0, 256})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.number(2000000) == 5);
    rt.tick(); REQUIRE(rt.number(2000000) == 5);
    rt.tick(); REQUIRE(rt.number(2000000) == 8); REQUIRE(rt.error().empty());
}
void branch_and_loop() {
    wolf::Runtime rt(project({cmd(111, {17, 1, 1, 2}), cmd(401, {1}), cmd(121, {2000000, 7, 0, 0}, 1),
        cmd(420, {0}), cmd(121, {2000000, 999, 0, 0}, 1), cmd(499), cmd(179, {3}),
        cmd(121, {2000000, 2, 0, 256}, 1), cmd(498)}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000000) == 13);
}
void self_banks_and_snapshot() {
    wolf::Runtime rt(project({cmd(180, {3}), cmd(121, {2000005, 99, 0, 0})}));
    wolf::Context first{-1, 2, 0, 1}, second{-1, 2, 0, 2};
    rt.set_number(1100000, 7, first); rt.set_number(1100000, 9, second);
    REQUIRE(rt.number(1100000, first) == 7); REQUIRE(rt.number(1100000, second) == 9);
    rt.set_text(3000000, "English / 日本語"); rt.set_number(1100000000, 1234);
    rt.start_common(0); rt.tick(); const auto saved = rt.snapshot();
    wolf::Runtime loaded(project({cmd(180, {3}), cmd(121, {2000005, 99, 0, 0})})); loaded.restore(saved);
    REQUIRE(loaded.snapshot() == saved); REQUIRE(loaded.number(1100000000) == 1234);
    REQUIRE(loaded.text(3000000) == "English / 日本語");
    loaded.tick(); loaded.tick(); REQUIRE(loaded.number(2000005) == 0);
    loaded.tick(); REQUIRE(loaded.number(2000005) == 99);
    auto bad = saved; bad["runs"][0]["stack"][0]["pc"] = 99999;
    const auto before = loaded.snapshot();
    bool rejected = false; try { loaded.restore(bad); } catch (...) { rejected = true; }
    REQUIRE(rejected); REQUIRE(loaded.snapshot() == before);
    wolf::Runtime different(project({cmd(180, {4})}));
    rejected = false; try { different.restore(saved); } catch (...) { rejected = true; }
    REQUIRE(rejected);
}
void unsupported_is_fatal() {
    wolf::Runtime rt(project({cmd(765432), cmd(121, {2000000, 999, 0, 0})}));
    rt.start_common(0); rt.tick(); REQUIRE(!rt.error().empty()); REQUIRE(rt.number(2000000) == 0);
    REQUIRE(rt.diagnostics().at("command").at("code") == 765432);
}
void atomic_overwrite() {
    const auto path = (std::filesystem::temp_directory_path() / "wolf-port-native-test-save.json").string();
    wolf::atomic_save(path, {{"sequence", 1}}); wolf::atomic_save(path, {{"sequence", 2}});
    REQUIRE(wolf::load_json(path).at("sequence") == 2); std::filesystem::remove(path);
}
void named_database_and_bounds() {
    auto p = project({cmd(250, {99, 0, 0, 65536, 77}, 0, {"", "live-table", "", ""}),
        cmd(121, {2000000, 4, 0, 1536}), cmd(121, {2000000, 2, 0, 1792})});
    p["databases"]["mutable"]["types"][0]["schema"]["name"] = "live-table";
    wolf::Runtime rt(p); rt.start_common(0); rt.tick();
    REQUIRE(rt.error().empty()); REQUIRE(rt.number(1100000000) == 77);
    REQUIRE(rt.number(2000000) == 2);
}
Json route_cmd(int code, Json args = Json::array()) {
    return {{"code", code}, {"args", args}, {"extra", {0}}};
}
Json route(const std::vector<Json>& commands, int options = 6) {
    auto result = cmd(201, {-2});
    result["route"] = {{"flags", {3, 3, 3, 0, 3, options}}, {"commands", Json(commands)}};
    return result;
}
void route_waits_and_persistence() {
    auto p = project({route({route_cmd(29, {5}), route_cmd(47, {2}), route_cmd(45, {99})}),
        cmd(121, {2000000, 42, 0, 0})});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick();
    REQUIRE(rt.error().empty()); REQUIRE(rt.characters().at("hero").at("speed") == 5);
    REQUIRE(rt.number(2000000) == 0);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); REQUIRE(loaded.snapshot() == rt.snapshot());
    loaded.tick(); REQUIRE(loaded.number(2000000) == 0);
    loaded.tick(); REQUIRE(loaded.error().empty()); REQUIRE(loaded.number(2000000) == 42);
    REQUIRE(loaded.characters().at("hero").at("opacity") == 99);
    // A wait as the last route instruction must also block the caller.
    wolf::Runtime tail(project({route({route_cmd(47, {2})}), cmd(121, {2000000, 7, 0, 0})}));
    tail.start_common(0); tail.tick(); tail.tick(); REQUIRE(tail.number(2000000) == 0);
    tail.tick(); if (!tail.error().empty()) throw std::runtime_error(tail.diagnostics().dump()); REQUIRE(tail.number(2000000) == 7);
    // Explicit WaitForMove waits for nonblocking routes as well.
    wolf::Runtime all(project({route({route_cmd(47, {2}), route_cmd(32)}, 2), cmd(202), cmd(121, {2000000, 8, 0, 0})}));
    all.start_common(0); all.tick(); all.tick(); REQUIRE(all.number(2000000) == 0);
    all.tick(); REQUIRE(all.number(2000000) == 8); REQUIRE(all.characters().at("hero").at("idle_animation") == true);
    // Unsupported movement stops before the following event command.
    wolf::Runtime bad(project({route({route_cmd(0)}), cmd(121, {2000000, 9, 0, 0})}));
    bad.start_common(0); bad.tick(); REQUIRE(!bad.error().empty()); REQUIRE(bad.number(2000000) == 0);
}
void database_counts_and_absent_rows() {
    auto p = project({cmd(250, {0, -1, 33, 4096, 2000000}),
        cmd(250, {0, 0, -1, 4096, 2000001}), cmd(121, {2000002, 123, 0, 0}),
        cmd(250, {0, 16, 0, 4096, 2000002})});
    auto& table = p["databases"]["mutable"]["types"][0];
    table["positions"] = {1000, 2000}; table["schema"]["fields"] = {"number", "text"};
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.number(2000000) == 1); REQUIRE(rt.number(2000001) == 2); REQUIRE(rt.number(2000002) == 0);
}
void saved_variables_survive_restart() {
    const auto temp = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto base = temp / ("wolf-variable-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(base.parent_path() == temp);
    auto p = project({cmd(221, {2000000, 3000000, 9000024, 0}),
        cmd(221, {2000003, 3000000, 2000005, 0}), cmd(221, {3000003, 3000000, 3000005, 0}),
        cmd(222, {77, 3000000, 2000005, 0}), cmd(222, {3000001, 3000000, 3000005, 0}),
        cmd(221, {2000001, 3000000, 9000024, 0}), cmd(221, {2000002, 3000000, 2000005, 0}),
        cmd(221, {3000002, 3000000, 3000005, 0})});
    wolf::Runtime rt(p, base.u8string()); rt.set_text(3000000, "Save/System.sav"); rt.set_text(3000001, "saved text");
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.number(2000000) == 0); REQUIRE(rt.number(2000001) == 1);
    REQUIRE(rt.number(2000002) == 77); REQUIRE(rt.text(3000002) == "saved text");
    wolf::Runtime restarted(p, base.u8string()); restarted.set_text(3000000, "save/system.sav");
    // A fresh interpreter reads the file before its write instructions run.
    restarted.start_common(0); restarted.tick(); REQUIRE(restarted.error().empty()); REQUIRE(restarted.number(2000000) == 1);
    REQUIRE(restarted.number(2000003) == 77); REQUIRE(restarted.text(3000003) == "saved text");
    const auto file = base / "save/system.sav.json";
    REQUIRE(wolf::load_json(file.u8string()).at("numbers").at("2000005") == 77);
    std::filesystem::remove_all(base);
}
void literal_indirect_and_real_arithmetic() {
    wolf::Runtime rt(project({cmd(121, {2000000, 2000005, 0, 4}),
        cmd(121, {2000000, 77, 0, 16}), cmd(121, {2000001, 2000000, 0, 32}),
        cmd(121, {2000002, 100, 0, 0}), cmd(121, {2000002, 3, 2, 12288 + 768 + 2}),
        cmd(121, {2000003, 15000000, 0, 5})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.number(2000005) == 77); REQUIRE(rt.number(2000001) == 77);
    REQUIRE(rt.number(2000002) == 150); REQUIRE(rt.number(2000003) == 999999);
}
void common_event_string_arguments() {
    auto p = project({cmd(300, {0, 0x2021, 42, 3000000, 0}, 0, {"callee", "", "literal"})});
    p["common"]["events"].push_back({{"id", 1}, {"name", "callee"}, {"mode", 0}, {"return_id", 1600000},
        {"commands", {cmd(122, {3000001, 0}, 0, {"\\cself[5]"}), cmd(122, {3000002, 0}, 0, {"\\cself[6]"}),
            cmd(121, {2000000, 1600000, 0, 0})}}});
    wolf::Runtime rt(p); rt.set_text(3000000, "variable"); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.text(3000001) == "variable"); REQUIRE(rt.text(3000002) == "literal"); REQUIRE(rt.number(2000000) == 42);
}
void map_events_precede_parallel_common_events() {
    auto p = project({cmd(121, {2000001, 2000000, 0, 0})});
    p["common"]["events"][0]["mode"] = 3;
    p["maps"][0]["events"].push_back({{"id", 0}, {"pages", {{{"trigger", 1}, {"switches", {0, 0, 0, 0}},
        {"commands", Json::array({cmd(121, {2000000, 7, 0, 0})})}}}}});
    wolf::Runtime rt(p); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000001) == 7);
    REQUIRE(rt.number(9000118) == 16); REQUIRE(rt.number(9000084) == -1); REQUIRE(rt.number(9000100) == 100);
}
void relative_common_calls() {
    auto p = project({cmd(210, {600102, 0}), cmd(121, {2000001, 1, 0, 0})});
    p["common"]["events"].push_back({{"id", 1}, {"name", "backward target"}, {"mode", 0}, {"return_id", 1600000},
        {"commands", Json::array({cmd(121, {2000000, 77, 0, 0})})}});
    p["common"]["events"].push_back({{"id", 2}, {"name", "forward target"}, {"mode", 0}, {"return_id", 1600000},
        {"commands", Json::array({cmd(210, {600099, 0})})}});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.number(2000000) == 77); REQUIRE(rt.number(2000001) == 1);
}
void string_conditions() {
    wolf::Runtime rt(project({cmd(112, {18, 3000000, 0x01000000 + 3000001, 3000002}, 0, {"wrong", ""}),
        cmd(401, {1}), cmd(121, {2000000, 1, 0, 0}, 1), cmd(401, {2}), cmd(121, {2000000, 2, 0, 0}, 1),
        cmd(420), cmd(121, {2000000, 3, 0, 0}, 1), cmd(499),
        cmd(112, {1, 0x30000000 + 3000000}, 0, {"prefix"}), cmd(401, {1}), cmd(121, {2000001, 7, 0, 0}, 1), cmd(499)}));
    rt.set_text(3000000, "prefix text"); rt.set_text(3000001, "equal"); rt.set_text(3000002, "equal");
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000000) == 2); REQUIRE(rt.number(2000001) == 7);
}
void strings_and_resource_files() {
    const auto temp = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto base = temp / ("wolf-resource-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(base.parent_path() == temp);
    std::filesystem::create_directories(base / "Data/Text");
    { std::ofstream file(base / "Data/Text/Example.txt", std::ios::binary); file << "\xef\xbb\xbf" "original\ntext"; }
    auto p = project({cmd(122, {3000001, 0, 0}, 0, {"\\s[0]"}),
        cmd(122, {3000001, 256, 0}, 0, {" appended"}),
        cmd(122, {3000002, 1, 3000001}), cmd(122, {3000003, 2, 2000000}),
        cmd(122, {2000001, 16, 0}, 0, {"indirect destination"}),
        cmd(122, {3000010, 1280, 0, 1}, 0, {"data\\text\\example.TXT"}),
        cmd(122, {3000011, 1281, 3000020}),
        cmd(122, {3000012, 1280, 0, 1}, 0, {"<<GET_FILE_EXIST>>DATA/Text"}),
        cmd(122, {3000013, 1280, 0, 1}, 0, {"<<GET_FILE_EXIST>>Data/Text/"}),
        cmd(122, {3000014, 1280, 0, 1}, 0, {"Data/Text/missing.txt"})});
    wolf::Runtime rt(p, {}, base.u8string()); rt.set_text(3000000, "original");
    rt.set_number(2000000, 3000002); rt.set_number(2000001, 3000004);
    rt.set_text(3000020, "<<GET_FILE_EXIST>>data/text/example.txt");
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.text(3000002) == "original appended"); REQUIRE(rt.text(3000003) == "original appended");
    REQUIRE(rt.text(3000004) == "indirect destination"); REQUIRE(rt.text(3000010) == "original\ntext");
    REQUIRE(rt.text(3000011) == "1"); REQUIRE(rt.text(3000012) == "1");
    REQUIRE(rt.text(3000013) == "0"); REQUIRE(rt.text(3000014) == "<NoFile>");
    wolf::Resources resources(base.u8string());
    bool rejected = false; try { resources.read_text("../outside.txt"); } catch (...) { rejected = true; }
    REQUIRE(rejected); std::filesystem::remove_all(base);
}
void picture_file_sources() {
    auto p = project({cmd(150, {0, 3, 0, 1, 1, 1, 255, 50, 60, 100, 0}, 0, {"Picture/literal.png"}),
        cmd(150, {16, 4, 0, 2, 1, 2, 128, 10, 20, 100, 0, 3000000}),
        cmd(150, {32, 5, 0, 1, 1, 1, 255, 0, 0, 100, 0}, 0, {"\\s[1]"})});
    wolf::Runtime rt(p); rt.set_text(3000000, "Picture/variable.png"); rt.set_text(3000001, "text picture");
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.pictures().at("3").at("content") == "Picture/literal.png");
    REQUIRE(rt.pictures().at("4").at("content") == "Picture/variable.png");
    REQUIRE(rt.pictures().at("4").at("pattern") == 2);
    REQUIRE(rt.pictures().at("5").at("content") == "text picture");
}
void picture_flicker_and_input_replay() {
    const auto p = project({cmd(150, {0, 5, 0, 1, 1, 1, 255, 0, 0, 100, 0}, 0, {"test.png"}),
        cmd(290, {80, 30, 4, 6, -50, -20, 0}), cmd(180, {100})});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(wolf::picture_tint(rt.pictures().at("5"), 1)[0] == 100);
    const auto low = wolf::picture_tint(rt.pictures().at("5"), 31);
    REQUIRE(low[0] == 50); REQUIRE(low[1] == 80); REQUIRE(low[2] == 100);
    REQUIRE(wolf::picture_tint(rt.pictures().at("5"), 61)[0] == 100);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); REQUIRE(loaded.pictures() == rt.pictures());
    wolf::InputReplay replay(Json::array({{{"frame", 2}, {"confirm", true}, {"mouse_left", true}, {"wheel", 1}},
        {{"frame", 4}, {"mouse_x", 42}}}));
    REQUIRE(!replay.input(0).confirm); const auto press = replay.input(2);
    REQUIRE(press.confirm && press.mouse_left_new && press.wheel == 1);
    const auto held = replay.input(3); REQUIRE(held.confirm && !held.mouse_left_new && held.wheel == 0);
    const auto released = replay.input(4); REQUIRE(!released.confirm && released.mouse_x == 42);
}
void delayed_picture_fade_and_sound() {
    const auto p = project({cmd(150, {0, 5, 0, 1, 1, 1, 200, 0, 0, 100, 0}, 0, {"test.png"}),
        cmd(150, {65538, 5, 4, 2}), cmd(140, {33554464, 0, 0, 7, 80, 120}, 0, {"SE/test.ogg"}), cmd(180, {100})});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    const auto audio = rt.take_audio(); REQUIRE(audio.size() == 1);
    REQUIRE(audio.at(0).at("file") == "SE/test.ogg"); REQUIRE(audio.at(0).at("channel") == 7);
    REQUIRE(audio.at(0).at("volume") == 80); REQUIRE(audio.at(0).at("tempo") == 120); REQUIRE(rt.take_audio().empty());
    REQUIRE(rt.pictures().at("5").at("opacity") == 200);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot());
    loaded.tick(); loaded.tick(); REQUIRE(loaded.pictures().at("5").at("opacity") == 200);
    loaded.tick(); REQUIRE(loaded.pictures().at("5").at("opacity") == 150);
    wolf::Runtime during(p); during.restore(loaded.snapshot());
    during.tick(); REQUIRE(during.pictures().at("5").at("opacity") == 100);
    during.tick(); REQUIRE(during.pictures().at("5").at("opacity") == 50);
    during.tick(); REQUIRE(during.pictures().empty()); REQUIRE(during.error().empty());
}
void choices_wait_and_restore() {
    auto p = project({cmd(102, {19}, 0, {"first", "second", "third"}),
        cmd(401, {2}), cmd(121, {2000000, 10, 0, 0}, 1),
        cmd(401, {3}), cmd(121, {2000000, 20, 0, 0}, 1),
        cmd(401, {4}), cmd(121, {2000000, 30, 0, 0}, 1), cmd(499)});
    p["databases"]["system"]["types"][3]["rows"] = Json::array({Json::array({"SE/confirm.wav", 90, 100, 0}), Json::array({"SE/cursor.wav", 50, 120, 0})});
    wolf::Runtime rt(p); rt.set_number(9000018, 1); rt.start_common(0);
    wolf::Input confirm; confirm.confirm = true;
    rt.tick(confirm); rt.tick(confirm); REQUIRE(rt.number(2000000) == 0); REQUIRE(rt.number(9000020) == 0);
    rt.tick(); wolf::Input down; down.down = true; rt.tick(down); REQUIRE(rt.number(9000020) == 1);
    REQUIRE(rt.take_audio().at(0).at("file") == "SE/cursor.wav");
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick(); loaded.tick(confirm);
    REQUIRE(loaded.number(2000000) == 0); loaded.tick(); REQUIRE(loaded.number(2000000) == 20);
    REQUIRE(loaded.number(9000020) == 1); loaded.tick(); REQUIRE(loaded.number(9000020) == -1); REQUIRE(loaded.error().empty());
    REQUIRE(loaded.take_audio().at(0).at("file") == "SE/confirm.wav");
}
void picture_interpolation_survives_resume() {
    const auto p = project({cmd(150, {0, 7, 4, 1, 1, 1, 200, 10, 20, 100, 0}, 0, {"test.png"}), cmd(180, {4}),
        cmd(150, {0, 7, 4, 1, 1, 1, 100, 30, 60, 200, 900}, 0, {"test.png"}), cmd(180, {100})});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.pictures().at("7").at("opacity") == 0);
    rt.tick(); rt.tick(); REQUIRE(rt.pictures().at("7").at("opacity") == 100);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick(); loaded.tick();
    REQUIRE(loaded.pictures().at("7").at("opacity") == 200);
    loaded.tick(); loaded.tick(); const auto halfway = loaded.pictures().at("7");
    REQUIRE(halfway.at("x") == 20); REQUIRE(halfway.at("y") == 40);
    REQUIRE(halfway.at("zoom") == 150); REQUIRE(halfway.at("angle") == 450); REQUIRE(halfway.at("opacity") == 150);
    loaded.tick(); loaded.tick(); REQUIRE(loaded.pictures().at("7").at("x") == 30);
    REQUIRE(!loaded.pictures().at("7").contains("tween")); REQUIRE(loaded.error().empty());
}
void transfer_preserves_caller_and_changes_map() {
    auto p = project({cmd(130, {-1, 7, 6, 1, 0}), cmd(121, {2000001, 42, 0, 0})});
    p["databases"]["system"]["types"][0]["rows"].push_back({"MapData/Other.mps", -2, -2});
    p["maps"][0]["events"].push_back({{"id", 0}, {"pages", {{{"trigger", 2}, {"switches", {0, 0, 0, 0}},
        {"commands", Json::array({cmd(180, {50}), cmd(121, {2000000, 99, 0, 0})})}}}}});
    const Json page = {{"trigger", 1}, {"switches", {0, 0, 0, 0}},
        {"commands", Json::array({cmd(121, {2000000, 7, 0, 0}), cmd(180, {100})})}};
    const Json event = {{"id", 0}, {"pages", Json::array({page})}};
    p["maps"].push_back({{"file", "MapData/Other.mps"}, {"events", Json::array({event})}});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000001) == 42);
    const auto state = rt.snapshot(); REQUIRE(state.at("map") == 1); REQUIRE(state.at("hero_x") == 14); REQUIRE(state.at("hero_y") == 12);
    REQUIRE(rt.characters().at("hero").at("x") == 14);
    wolf::Runtime loaded(p); loaded.restore(state); loaded.tick(); REQUIRE(loaded.number(2000000) == 7);
    for (int i = 0; i < 51; ++i) loaded.tick();
    REQUIRE(loaded.number(2000000) == 7); REQUIRE(loaded.error().empty());
}
void picture_ranges_match_windows() {
    std::vector<Json> commands;
    for (int id = -3; id <= 8; ++id) if (id != 0)
        commands.push_back(cmd(150, {0, id, 0, 1, 1, 1, 255, 0, 0, 100, 0}, 0, {"test.png"}));
    commands.push_back(cmd(150, {16777218, 3, 0, 0, 0, 5}));
    commands.push_back(cmd(150, {16777218, -1, 0, 0, 0, -2}));
    wolf::Runtime rt(project(commands)); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.pictures().size() == 6);
    for (int id : {-3, 1, 2, 6, 7, 8}) REQUIRE(rt.pictures().contains(std::to_string(id)));
}
void nested_condition_breaks_nearest_loop() {
    wolf::Runtime rt(project({cmd(179, {2}), cmd(179, {5}, 1), cmd(111, {1, 1, 1, 2}, 2),
        cmd(401, {1}, 2), cmd(121, {2000000, 1, 0, 256}, 3), cmd(171, {}, 3), cmd(499, {}, 2),
        cmd(498, {}, 1), cmd(121, {2000001, 1, 0, 256}, 1), cmd(498)}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000000) == 2); REQUIRE(rt.number(2000001) == 2);
}
void picture_hover_uses_original_dimensions() {
    const auto temp = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto base = temp / ("wolf-picture-query-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(base.parent_path() == temp); std::filesystem::create_directories(base / "Data");
    const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,8,0,0,0,6,8,6,0,0,0,254,5,223,251,0,0,0,20,73,68,65,84,120,156,99,248,255,255,63,3,62,140,87,114,176,40,0,0,196,7,143,113,180,214,159,217,0,0,0,0,73,69,78,68,174,66,96,130};
    { std::ofstream file(base / "Data/picture.png", std::ios::binary); file.write(reinterpret_cast<const char*>(png), sizeof(png)); }
    auto p = project({cmd(150, {4096, 3, 0, 2, 1, 1, 0, 20, 30, 200, 0}, 0, {"PICTURE.png"}),
        cmd(124, {2000000, 16384, 3, 8}), cmd(124, {2000001, 16384, 3, 2}), cmd(124, {2000002, 16384, 3, 3}),
        cmd(124, {2000003, 16384, 3, 9}), cmd(124, {2000004, 16384, 100, 8})});
    wolf::Runtime rt(p, {}, base.u8string()); wolf::Input input; input.mouse_x = 16; input.mouse_y = 24;
    rt.start_common(0); rt.tick(input); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000000) == 1);
    REQUIRE(rt.number(2000001) == 4); REQUIRE(rt.number(2000002) == 6);
    REQUIRE(rt.number(2000003) == 1); REQUIRE(rt.number(2000004) == -1);
    input.mouse_x = 24; rt.start_common(0); rt.tick(input); REQUIRE(rt.number(2000000) == 0);
    input.mouse_x = 20; input.mouse_y = 36; rt.start_common(0); rt.tick(input); REQUIRE(rt.number(2000000) == 0);
    REQUIRE(rt.error().empty()); std::filesystem::remove_all(base);
}
void message_reveal_waits_and_restores() {
    const auto p = project({cmd(101, {}, 0, {"Hello \\s[0]"}), cmd(121, {2000000, 77, 0, 0})});
    wolf::Runtime rt(p); rt.set_number(9000008, 20); rt.set_number(9000009, 60); rt.set_number(9000010, 2);
    rt.set_text(9900026, "\\E"); rt.set_text(3000000, "world"); rt.start_common(0);
    rt.tick(); rt.tick(); REQUIRE(rt.number(2000000) == 0); REQUIRE(rt.message_state().at("visible") == 0);
    rt.tick(); REQUIRE(rt.message_state().at("visible") == 1); REQUIRE(rt.number(9000012) == 1);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); wolf::Input confirm; confirm.confirm = true;
    loaded.tick(confirm); REQUIRE(loaded.message_state().at("complete") == true); REQUIRE(loaded.number(2000000) == 0);
    loaded.tick(confirm); REQUIRE(loaded.number(9000012) == 1); loaded.tick(); loaded.tick(confirm);
    REQUIRE(loaded.number(9000012) == 0); loaded.tick(); REQUIRE(loaded.message_state().empty()); REQUIRE(loaded.number(2000000) == 77);
    REQUIRE(loaded.text(9900025) == "\\EHello world"); REQUIRE(loaded.error().empty());
    wolf::Runtime automatic(project({cmd(101, {}, 0, {"@5\n\\>short\\^"}), cmd(121, {2000000, 1, 0, 0})}));
    automatic.start_common(0); automatic.tick(); REQUIRE(automatic.number(9000000) == 5); automatic.tick();
    REQUIRE(automatic.number(2000000) == 1); REQUIRE(automatic.error().empty());
}
void database_and_many_text_substitutions() {
    auto p = project({});
    p["databases"]["user"]["types"] = Json::array({{{"schema", {{"name", "table"}, {"names", {"row"}}}},
        {"rows", Json::array({Json::array({7, "style \\v[1]"})})}}});
    wolf::Runtime rt(p); rt.set_number(2000000, 1); rt.set_number(2000001, 42); rt.set_number(2000002, 0);
    REQUIRE(rt.expand("\\udb[\\v[2]:0:0]", {}) == "7"); REQUIRE(rt.expand("\\udb[0:0:1]", {}) == "style 42");
    REQUIRE(rt.expand("\\udb[0]/\\udb[0:0]", {}) == "table/row");
    std::string many; for (int i = 0; i < 100; ++i) many += "\\v[0] ";
    REQUIRE(rt.expand(many, {}).size() == 200); REQUIRE(rt.expand("\\\\v[0]", {}) == "\\\\v[0]");
    rt.set_text(3000000, "\\s[0]"); bool rejected = false; try { rt.expand("\\s[0]", {}); } catch (...) { rejected = true; }
    REQUIRE(rejected);
}
void picture_delay_reset_splits_pending_range() {
    std::vector<Json> commands;
    for (int id = 1; id <= 3; ++id) commands.push_back(cmd(150, {0, id, 0, 1, 1, 1, 255, 0, 0, 100, 0}, 0, {"test.png"}));
    commands.push_back(cmd(150, {16777218, 1, 0, 2, 0, 3})); commands.push_back(cmd(150, {3, 2}));
    commands.push_back(cmd(180, {100}));
    const auto p = project(commands); wolf::Runtime rt(p); rt.start_common(0); rt.tick();
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick(); loaded.tick();
    REQUIRE(loaded.error().empty()); REQUIRE(loaded.pictures().size() == 1); REQUIRE(loaded.pictures().contains("2"));
}
void picture_move_preserves_graphic_and_same_values() {
    wolf::Runtime rt(project({cmd(150, {0, 3, 0, 1, 1, 1, 200, 10, 20, 100, 0}, 0, {"test.png"}),
        cmd(150, {65537, 3, 2, 0, 0, -1000000, -1000000, 4, -2, -1000000, -1000000}), cmd(180, {50})}));
    rt.start_common(0); rt.tick(); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.pictures().at("3").at("x") == 12); REQUIRE(rt.pictures().at("3").at("y") == 19);
    rt.tick(); REQUIRE(rt.pictures().at("3").at("x") == 14); REQUIRE(rt.pictures().at("3").at("y") == 18);
    REQUIRE(rt.pictures().at("3").at("opacity") == 200); REQUIRE(rt.pictures().at("3").at("zoom") == 100);
    REQUIRE(rt.pictures().at("3").at("content") == "test.png");
    wolf::Runtime same(project({cmd(150, {0, 3, 0, 1, 1, 1, 200, 10, 20, 125, 0}, 0, {"test.png"}),
        cmd(150, {4259841, 3, 0, 0, 0, 1, 0, 0, 0, 50, 0, 0, 16777216})}));
    same.start_common(0); same.tick(); REQUIRE(same.error().empty());
    REQUIRE(same.pictures().at("3").at("zoom") == 125); REQUIRE(same.pictures().at("3").at("opacity") == 0);
}
void delayed_text_picture_reveal_survives_restore() {
    const auto p = project({cmd(121, {2000000, 10, 0, 0}),
        cmd(150, {3145760, 7, 0, 1, 1, 1, 255, 2000000, 30, 80, 0, 0, 33554432, 2, 0, 50, 100, 75, 120}, 0, {"\\ax[120]\n\\ay[0]\\sp[60]\\EAB\xc3\xa9"}),
        cmd(121, {2000000, 99, 0, 0}), cmd(180, {100})});
    wolf::Runtime rt(p); rt.set_number(9000008, 20); rt.start_common(0); rt.tick();
    REQUIRE(rt.error().empty()); REQUIRE(rt.pictures().at("7").at("kind") == -1); rt.tick();
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick();
    REQUIRE(loaded.pictures().at("7").at("x") == 10); REQUIRE(loaded.pictures().at("7").at("zoom_y") == 120);
    REQUIRE(loaded.pictures().at("7").at("red") == 50); REQUIRE(loaded.pictures().at("7").at("text_style").at("visible_chars") == 0);
    loaded.tick(); REQUIRE(loaded.pictures().at("7").at("text_style").at("visible_chars") == 1);
    loaded.tick(); loaded.tick(); REQUIRE(loaded.pictures().at("7").at("text_style").at("visible_chars") == 3);
    REQUIRE(loaded.error().empty());
    const auto times = text_reveal_times("A\\sp[60]B\\>CD\\<E", 0);
    REQUIRE(times.size() == 5); REQUIRE(times[0] == 0); REQUIRE(times[1] == times[2]); REQUIRE(times[2] == times[3]); REQUIRE(times[4] > times[3]);
}
void delayed_moves_use_current_position_and_can_be_cancelled() {
    wolf::Runtime rt(project({cmd(150, {0, 7, 0, 1, 1, 1, 255, 10, 0, 100, 0}, 0, {"test.png"}),
        cmd(150, {65537, 7, 0, 0, 0, -1000000, -1000000, 4, 0, -1000000, -1000000, 0, 16777216, 2}),
        cmd(150, {1, 7, 0, 0, 0, -1000000, -1000000, 20, 0, -1000000, -1000000}),
        cmd(150, {0, 8, 0, 1, 1, 1, 255, 0, 0, 100, 0, 0, 16777216, 2}, 0, {"cancelled.png"}),
        cmd(150, {3, 8}), cmd(180, {50})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.pictures().at("7").at("x") == 20);
    rt.tick(); rt.tick(); REQUIRE(rt.pictures().at("7").at("x") == 24); REQUIRE(rt.pictures().at("8").at("kind") == -1);
}
void pending_pictures_accept_effects_and_queries_like_windows() {
    const auto p = project({cmd(150, {32, 7, 0, 1, 1, 1, 255, 50, 150, 100, 0, 0, 33554432, 2, 0, 100, 100, 100}, 0, {"Delayed text"}),
        cmd(290, {32, 0, 7, 7, 50, 20, 0}), cmd(124, {2000000, 16384, 7, 9}), cmd(124, {2000001, 16384, 7, 10}),
        cmd(124, {2000002, 16384, 7, 0}), cmd(124, {2000003, 16384, 7, 5}), cmd(124, {2000004, 16384, 999, 8}), cmd(180, {100})});
    wolf::Runtime rt(p); rt.set_number(9000008, 20); rt.start_common(0); rt.tick();
    REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000000) == 1); REQUIRE(rt.number(2000001) == -1);
    REQUIRE(rt.number(2000002) == 0); REQUIRE(rt.number(2000003) == 0); REQUIRE(rt.number(2000004) == -1);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick(); loaded.tick();
    REQUIRE(loaded.pictures().at("7").at("kind") == 2); REQUIRE(loaded.pictures().at("7").at("x") == 50);
    REQUIRE(loaded.pictures().at("7").at("draw_shift").at("x") == 50); REQUIRE(loaded.pictures().at("7").at("draw_shift").at("y") == 20);
    REQUIRE(loaded.error().empty());
}
void picture_effects_are_separate_from_query_values() {
    const auto p = project({cmd(150, {0, 3, 0, 4, 1, 2, 255, 20, 30, 100, 0}, 0, {"test.png"}),
        cmd(290, {32, 4, 3, 3, 12, -4, 0}), cmd(290, {160, 1, 3, 3, 1, 4, 0}),
        cmd(124, {2000000, 16384, 3, 0}), cmd(124, {2000001, 16384, 3, 4}), cmd(180, {50})});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.number(2000000) == 20); REQUIRE(rt.number(2000001) == 2);
    REQUIRE(rt.pictures().at("3").at("draw_shift").at("x") == 3);
    REQUIRE(wolf::picture_pattern(rt.pictures().at("3"), 3) == 4); REQUIRE(wolf::picture_pattern(rt.pictures().at("3"), 4) == 3);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick(); loaded.tick(); loaded.tick();
    REQUIRE(loaded.pictures().at("3").at("draw_shift").at("x") == 12); REQUIRE(loaded.pictures().at("3").at("x") == 20);
}
void ranged_move_includes_existing_ids_when_first_is_missing() {
    wolf::Runtime rt(project({cmd(150, {0, 2, 0, 1, 1, 1, 255, 20, 0, 125, 0}, 0, {"test.png"}),
        cmd(150, {0, 3, 0, 1, 1, 1, 255, 30, 0, 125, 0}, 0, {"test.png"}),
        cmd(150, {21037057, 0, 0, 0, 0, -1000000, -1000000, 5, 0, 999, -1000000, 0, 16777216, 0, 4})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.pictures().at("2").at("x") == 25); REQUIRE(rt.pictures().at("3").at("x") == 35);
    REQUIRE(rt.pictures().at("3").at("zoom") == 125);
}
void basic_and_mouse_input_permissions() {
    const auto p = project({cmd(126, {513}), cmd(123, {2000000, 16}), cmd(126, {1}), cmd(123, {2000001, 16}),
        cmd(126, {268435713}), cmd(123, {2000002, 784}), cmd(126, {268435457}), cmd(123, {2000003, 784}),
        cmd(126, {752}), cmd(123, {2000004, 1}), cmd(180, {100})});
    wolf::Runtime rt(p); wolf::Input input; input.confirm = true; input.down = true; input.mouse_left = input.mouse_left_new = true;
    rt.start_common(0); rt.tick(input); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000000) == 0); REQUIRE(rt.number(2000001) == 10);
    REQUIRE(rt.number(2000002) == 0); REQUIRE(rt.number(2000003) == 20); REQUIRE(rt.number(2000004) == 0);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.start_common(0); loaded.tick(input); REQUIRE(loaded.error().empty());
    REQUIRE(loaded.number(2000004) == 0);
}
void automatic_basic_input_is_deferred_and_restored() {
    const auto p = project({cmd(125, {1}), cmd(123, {2000000, 16}), cmd(180, {1}),
        cmd(123, {2000001, 16}), cmd(180, {1}), cmd(123, {2000002, 16})});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.number(2000000) == 0);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick(); REQUIRE(loaded.number(2000001) == 10);
    loaded.tick(); REQUIRE(loaded.number(2000002) == 0); REQUIRE(loaded.error().empty());
}
void delayed_se_dispatch_survives_restore() {
    const auto p = project({cmd(140, {33554464, 2, 0, 0, 100, 100}, 0, {"SE/test.ogg"}), cmd(180, {50})});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.take_audio().empty());
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick(); REQUIRE(loaded.take_audio().empty());
    loaded.tick(); const auto audio = loaded.take_audio(); REQUIRE(audio.size() == 1); REQUIRE(audio.at(0).at("file") == "SE/test.ogg");
    loaded.tick(); REQUIRE(loaded.take_audio().empty());
}
void music_stop_preserves_kind_and_fade_duration() {
    wolf::Runtime rt(project({cmd(140, {16776960, 100, 0, 0}), cmd(140, {16776976, 0, 0, 0})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); const auto audio = rt.take_audio();
    REQUIRE(audio.size() == 2); REQUIRE(audio.at(0).at("operation") == "stop"); REQUIRE(audio.at(0).at("kind") == 0);
    REQUIRE(audio.at(0).at("fade_ms") == 1666); REQUIRE(audio.at(1).at("kind") == 1); REQUIRE(audio.at(1).at("fade_ms") == 0);
}
void zero_sound_frequency_matches_windows_default() {
    wolf::Runtime rt(project({cmd(140, {33554432, 10, 0, 0, 70, 0, 0}, 0, {"BGM/sparrows.mp3"})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); const auto audio = rt.take_audio();
    REQUIRE(audio.size() == 1); REQUIRE(audio.at(0).at("tempo") == 100); REQUIRE(audio.at(0).at("volume") == 70);
    REQUIRE(rt.number(9000095) == 70); REQUIRE(rt.number(9000096) == 100);
}
void numeric_condition_literal_rhs_can_look_like_a_variable() {
    wolf::Runtime rt(project({cmd(111, {17, 2000001, 2000000, 18}), cmd(401, {1}), cmd(121, {2000002, 1, 0, 0}, 1),
        cmd(420, {0}), cmd(121, {2000002, 2, 0, 0}, 1), cmd(499),
        cmd(111, {17, 2000001, 2000000, 2}), cmd(401, {1}), cmd(121, {2000003, 1, 0, 0}, 1),
        cmd(420, {0}), cmd(121, {2000003, 2, 0, 0}, 1), cmd(499)}));
    rt.set_number(2000000, 9); rt.set_number(2000001, 2000000); rt.start_common(0); rt.tick();
    REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000002) == 1); REQUIRE(rt.number(2000003) == 2);
}
void arithmetic_ranges_match_windows_endpoints_and_aliasing() {
    wolf::Runtime rt(project({cmd(121, {2000001, 7, 0, 65536, 2}),
        cmd(121, {2000010, 2000010, 0, 65792, 2}), cmd(121, {1600040, 0, 0, 65536, 2})}));
    for (int i = 0; i < 6; ++i) rt.set_number(2000000 + i, 99);
    for (int i = 0; i < 3; ++i) rt.set_number(2000010 + i, i + 2);
    for (int i = 39; i <= 43; ++i) rt.set_number(1600000 + i, 77, {0});
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    for (int i = 0; i < 6; ++i) REQUIRE(rt.number(2000000 + i) == (i >= 1 && i <= 3 ? 7 : 99));
    REQUIRE(rt.number(2000010) == 4); REQUIRE(rt.number(2000011) == 7); REQUIRE(rt.number(2000012) == 8);
    for (int i = 39; i <= 43; ++i) REQUIRE(rt.number(1600000 + i, {0}) == (i >= 40 && i <= 42 ? 0 : 77));
}
void arithmetic_range_random_draws_match_repeated_assignments() {
    wolf::Runtime ranged(project({cmd(121, {2000020, 0, 100, 90112, 3})}));
    std::vector<Json> commands; for (int i = 0; i < 4; ++i) commands.push_back(cmd(121, {2000020 + i, 0, 100, 24576}));
    wolf::Runtime single(project(commands)); ranged.start_common(0); single.start_common(0); ranged.tick(); single.tick();
    REQUIRE(ranged.error().empty()); REQUIRE(single.error().empty());
    for (int i = 0; i < 4; ++i) REQUIRE(ranged.number(2000020 + i) == single.number(2000020 + i));
    REQUIRE(ranged.number(2000020) != ranged.number(2000021));
}
void numeric_common_range_preserves_strings_and_other_events() {
    wolf::Runtime rt(project({cmd(121, {1600000, 0, 0, 65536, 90})}));
    rt.set_text(1600005, "keep", {0}); rt.set_number(1600004, 5, {0}); rt.set_number(1600090, 7, {0}); rt.set_number(1600010, 9, {1});
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.text(1600005, {0}) == "keep");
    REQUIRE(rt.number(1600004, {0}) == 0); REQUIRE(rt.number(1600090, {0}) == 0); REQUIRE(rt.number(1600010, {1}) == 9);
}
void picture_shake_matches_windows_samples_and_resume() {
    auto p = project({cmd(150, {0, 3, 0, 1, 1, 1, 255, 20, 30, 100, 0}, 0, {"test.png"}),
        cmd(290, {32, 0, 3, 3, 4, -2, 0}), cmd(290, {48, 20, 3, 3, 0, 10, 99999}), cmd(180, {1000})});
    auto reset = p["common"]["events"][0]; reset["id"] = 1; reset["commands"] = Json::array({cmd(290, {48, 0, 3, 3, 0, 0, 0})});
    p["common"]["events"].push_back(reset);
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    // Frame/offset pairs captured from Game.exe with a rendered frame marker.
    const std::vector<std::pair<int, int>> samples{{0,1},{2,2},{18,10},{21,9},{23,8},{38,0},{45,-3},{59,-10},{60,-9},{65,-7},{79,0},{125,-3},{145,-7},{183,8},{205,-3},{233,-3}};
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot());
    for (const auto& [frame, expected] : samples) {
        const auto offset = wolf::picture_draw_offset(loaded.pictures().at("3"), frame + 1);
        REQUIRE(offset.first == 4); REQUIRE(offset.second == expected - 2);
    }
    loaded.start_common(1); loaded.tick(); REQUIRE(loaded.error().empty());
    REQUIRE(!loaded.pictures().at("3").contains("shake")); REQUIRE(wolf::picture_draw_offset(loaded.pictures().at("3"), loaded.frame()).second == -2);
}
void finite_picture_shake_returns_to_base_without_changing_queries() {
    wolf::Runtime rt(project({cmd(150, {0, 3, 0, 1, 1, 1, 255, 20, 30, 100, 0}, 0, {"test.png"}),
        cmd(290, {48, 2, 3, 3, 4, -6, 1}), cmd(180, {1}), cmd(124, {2000000, 16384, 3, 0}), cmd(124, {2000001, 16384, 3, 1})}));
    rt.start_common(0); rt.tick(); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000000) == 20); REQUIRE(rt.number(2000001) == 30);
    REQUIRE(wolf::picture_draw_offset(rt.pictures().at("3"), 2).first == 2); REQUIRE(wolf::picture_draw_offset(rt.pictures().at("3"), 2).second == -3);
    REQUIRE(wolf::picture_draw_offset(rt.pictures().at("3"), 3).first == 0); REQUIRE(wolf::picture_draw_offset(rt.pictures().at("3"), 100).second == 0);
}
void choice_mouse_selects_clicked_row_and_rejects_outside_click() {
    const auto p = project({cmd(102, {18}, 0, {"first", "second"}), cmd(401, {2}), cmd(121, {2000000, 10, 0, 0}, 1),
        cmd(401, {3}), cmd(121, {2000000, 20, 0, 0}, 1), cmd(499)});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); auto state = rt.snapshot();
    state["choice"]["layout"] = {{"x", 100}, {"y", 100}, {"width", 200}, {"pad_y", 14},
        {"rows", Json::array({{{"top", 0}, {"height", 26}}, {{"top", 26}, {"height", 26}}})}};
    wolf::Runtime loaded(p); loaded.restore(state); wolf::Input click; click.mouse_left_new = true;
    loaded.tick(click); REQUIRE(loaded.choice().at("done") == false);
    loaded.tick(); click.mouse_x = 150; click.mouse_y = 150; loaded.tick(click);
    REQUIRE(loaded.choice().at("selected") == 1); REQUIRE(loaded.choice().at("done") == true);
    loaded.tick(); REQUIRE(loaded.number(2000000) == 20); REQUIRE(loaded.error().empty());
}
void picture_show_retains_fields_and_uses_defaults_for_new_ids() {
    wolf::Runtime rt(project({
        cmd(150, {0x1100, 1, 0, 3, 2, 4, 180, 20, 30, 125, 450}, 0, {"old.png"}),
        cmd(150, {0xff00, 1, 0, 3, 2, -1000000, -1000000, -1000000, -1000000, -1000000, -1000000}, 0, {"new.png"}),
        cmd(150, {0xff20, 2, 0, 1, 1, -1000000, -1000000, -1000000, -1000000, -1000000, -1000000}, 0, {"text"})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    const auto& old = rt.pictures().at("1");
    REQUIRE(old.at("content") == "new.png"); REQUIRE(old.at("blend") == 1); REQUIRE(old.at("anchor") == 1);
    REQUIRE(old.at("pattern") == 4); REQUIRE(old.at("opacity") == 180); REQUIRE(old.at("x") == 20);
    REQUIRE(old.at("y") == 30); REQUIRE(old.at("zoom") == 125); REQUIRE(old.at("angle") == 450);
    const auto& fresh = rt.pictures().at("2");
    REQUIRE(fresh.at("blend") == 0); REQUIRE(fresh.at("anchor") == 0); REQUIRE(fresh.at("pattern") == 1);
    REQUIRE(fresh.at("opacity") == 255); REQUIRE(fresh.at("x") == 0); REQUIRE(fresh.at("zoom_y") == 100);
}
void delayed_picture_show_retains_values_at_activation() {
    wolf::Runtime rt(project({
        cmd(150, {0x100, 1, 0, 1, 1, 1, 180, 20, 30, 125, 450}, 0, {"old.png"}),
        cmd(150, {0xf00, 1, 0, 1, 1, 1, -1000000, -1000000, -1000000, -1000000, -1000000, 0, 0, 2}, 0, {"new.png"}),
        cmd(150, {0x3001, 1, 0, 1, 1, 1, 90, 80, 70, 200, 900})}));
    rt.start_common(0); rt.tick(); rt.tick(); rt.tick(); REQUIRE(rt.error().empty());
    const auto& picture = rt.pictures().at("1");
    REQUIRE(picture.at("content") == "new.png"); REQUIRE(picture.at("blend") == 0);
    REQUIRE(picture.at("opacity") == 90); REQUIRE(picture.at("x") == 80); REQUIRE(picture.at("zoom") == 200);
    REQUIRE(picture.at("angle") == 900);
}
void full_game_save_survives_restart_and_shares_partial_variables() {
    const auto base = std::filesystem::temp_directory_path() / ("wolf-full-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto p = project({cmd(121, {2000000, 41, 0, 0}), cmd(121, {1100000000, 987, 0, 0}),
        cmd(122, {3000001, 0}, 0, {"saved text"}), cmd(130, {-1, 5, 7, 0, 1}),
        cmd(150, {0, 1, 0, 1, 1, 1, 255, 20, 30, 100, 0}, 0, {"test.png"}),
        cmd(140, {0x02000000, 0, 0, 0, 40, 100, 0}, 0, {"music.ogg"}), cmd(180, {3}),
        cmd(220, {0, 3000007}), cmd(121, {2000000, 999, 0, 0}), cmd(180, {1000})});
    auto loader = p["common"]["events"][0]; loader["id"] = 1;
    loader["commands"] = Json::array({cmd(220, {1, 3000007}), cmd(121, {2000099, 999, 0, 0})});
    p["common"]["events"].push_back(loader);
    auto partial = loader; partial["id"] = 2;
    partial["commands"] = Json::array({cmd(221, {2000005, 3000007, 2000000, 0}),
        cmd(221, {2000006, 3000007, 9000024, 0}), cmd(222, {2000007, 3000007, 2000000, 0})});
    p["common"]["events"].push_back(partial);
    {
        wolf::Runtime writer(p, base.u8string()); writer.set_text(3000007, "Save/Slot1.sav");
        writer.set_number(1100000, 23, {-1, 2, 0, 0}); writer.start_common(0);
        for (int i = 0; i < 4; ++i) writer.tick();
        REQUIRE(writer.error().empty()); REQUIRE(writer.number(2000000) == 999);
        REQUIRE(std::filesystem::exists(base / "save/slot1.sav.json"));
    }
    wolf::Runtime reader(p, base.u8string()); reader.set_text(3000007, "save/SLOT1.SAV");
    reader.set_number(2000007, 77); reader.start_common(2); reader.tick();
    REQUIRE(reader.error().empty()); REQUIRE(reader.number(2000005) == 41); REQUIRE(reader.number(2000006) == 1);
    reader.start_common(1); reader.tick(); REQUIRE(reader.error().empty());
    REQUIRE(reader.number(2000000) == 77); REQUIRE(reader.number(2000099) == 0);
    REQUIRE(reader.number(1100000000) == 987); REQUIRE(reader.text(3000001) == "saved text");
    REQUIRE(reader.number(1100000, {-1, 2, 0, 0}) == 23);
    const auto restored = reader.snapshot();
    REQUIRE(restored.at("hero_x") == 5); REQUIRE(restored.at("hero_y") == 7);
    REQUIRE(restored.at("runs").empty()); REQUIRE(restored.at("pictures").at("1").at("x") == 20);
    REQUIRE(restored.at("play_time_ticks") == 3);
    const auto audio = reader.take_audio(); REQUIRE(audio.size() == 4);
    REQUIRE(audio.at(3).at("operation") == "play"); REQUIRE(audio.at(3).at("resume_position_ms") == 50.0);
    std::filesystem::remove_all(base);
}
void missing_game_save_continues_and_parallel_load_is_rejected() {
    const auto p = project({cmd(220, {1, 3}), cmd(121, {2000000, 77, 0, 0})});
    wolf::Runtime missing(p); missing.start_common(0); missing.tick();
    REQUIRE(missing.error().empty()); REQUIRE(missing.number(2000000) == 77);
    auto parallel = p; parallel["common"]["events"][0]["mode"] = 3;
    wolf::Runtime invalid(parallel); invalid.tick(); REQUIRE(!invalid.error().empty());
    REQUIRE(invalid.number(2000000) == 0);
}
void saved_database_fields_use_the_slot_not_live_memory() {
    const auto temporary = std::filesystem::temp_directory_path();
    const auto base = temporary / ("wolf-saved-db-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto p = project({cmd(220, {0, 2}), cmd(180, {100})});
    auto reader = p["common"]["events"][0]; reader["id"] = 1;
    reader["commands"] = Json::array({cmd(221, {2000001, 2, 2000005, 1}),
        cmd(221, {3000001, 2, 2000006, 1}), cmd(221, {2000002, 2, 2000007, 1})});
    p["common"]["events"].push_back(reader);
    {
        wolf::Runtime writer(p, base.u8string()); writer.set_number(1100000000, 81);
        writer.set_text(1100000001, "slot value"); writer.set_number(2000010, 97);
        writer.start_common(0); writer.tick(); REQUIRE(writer.error().empty());
    }
    wolf::Runtime rt(p, base.u8string()); rt.set_number(1100000000, 999); rt.set_text(1100000001, "live value");
    rt.set_number(2000005, 1100000000); rt.set_number(2000006, 1100000001); rt.set_number(2000007, 2000010);
    rt.start_common(1); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.number(2000001) == 81); REQUIRE(rt.text(3000001) == "slot value"); REQUIRE(rt.number(2000002) == 97);
    REQUIRE(rt.number(1100000000) == 999); REQUIRE(rt.text(1100000001) == "live value");
    REQUIRE(std::filesystem::equivalent(base.parent_path(), temporary)); std::filesystem::remove_all(base);
}
void game_load_discards_pending_work_and_rebases_picture_ages() {
    const auto base = std::filesystem::temp_directory_path() / ("wolf-load-reset-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto p = project({cmd(150, {0, 1, 0, 1, 1, 1, 255, 20, 30, 100, 0}, 0, {"test.png"}),
        cmd(290, {48, 20, 1, 1, 0, 10, 99999}),
        cmd(150, {1, 1, 0, 1, 1, 1, 255, 90, 90, 100, 0, 0, 0, 10}),
        cmd(140, {0x02000020, 10, 0, 0, 100, 100}, 0, {"se.wav"}),
        cmd(220, {0, 3}), cmd(180, {1000})});
    auto loader = p["common"]["events"][0]; loader["id"] = 1;
    loader["commands"] = Json::array({cmd(220, {1, 3})}); p["common"]["events"].push_back(loader);
    wolf::Runtime rt(p, base.u8string()); rt.start_common(0); rt.tick();
    REQUIRE(rt.error().empty()); const auto initial = rt.snapshot();
    REQUIRE(initial.at("pending_picture_changes").size() == 1); REQUIRE(initial.at("pending_audio").size() == 1);
    for (int i = 0; i < 25; ++i) rt.tick();
    rt.start_common(1); rt.tick(); REQUIRE(rt.error().empty()); const auto loaded = rt.snapshot();
    REQUIRE(loaded.at("runs").empty()); REQUIRE(loaded.at("routes").empty());
    REQUIRE(loaded.at("pending_picture_changes").empty()); REQUIRE(loaded.at("pending_audio").empty());
    REQUIRE(rt.pictures().at("1").at("x") == 20); REQUIRE(rt.frame() == 27);
    REQUIRE(wolf::picture_draw_offset(rt.pictures().at("1"), 28) == wolf::picture_draw_offset(initial.at("pictures").at("1"), 1));
    std::filesystem::remove_all(base);
}
void corrupted_full_save_does_not_replace_live_state() {
    const auto base = std::filesystem::temp_directory_path() / ("wolf-invalid-load-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto p = project({cmd(220, {1, 3})}); wolf::Runtime rt(p, base.u8string());
    auto bad = rt.snapshot(); bad["project_id"] = "different game";
    wolf::atomic_save((base / "save/savedata03.sav.json").u8string(), {{"format", "wolf-native-variables"}, {"version", 1},
        {"project_id", rt.snapshot().at("project_id")}, {"numbers", Json::object()}, {"strings", Json::object()}, {"game", bad}});
    rt.set_number(2000009, 55); rt.start_common(0); rt.tick();
    REQUIRE(!rt.error().empty()); REQUIRE(rt.number(2000009) == 55);
    std::filesystem::remove_all(base);
}
void builtin_shapes_have_expected_gradients_and_transparency() {
    const auto gradient = wolf::shape_pixels("<GRADY-779-111>", 3, 5);
    REQUIRE(gradient.at(0) == 198); REQUIRE(gradient.at(2) == 255); REQUIRE(gradient.at(4 * 3 * 4) == 28);
    const auto frame = wolf::shape_pixels("<SQUARE><FRAME>", 3, 3);
    REQUIRE(frame.at(3) == 255); REQUIRE(frame.at((1 * 3 + 1) * 4 + 3) == 0);
    const auto circle = wolf::shape_pixels("<CIRCLE>", 10, 10);
    REQUIRE(circle.at(3) == 0); REQUIRE(circle.at((5 * 10 + 5) * 4 + 3) == 255);
    REQUIRE(!wolf::builtin_shape("<GRADY-ABC-111>"));
}
void writable_resources_override_assets_without_general_parent_traversal() {
    const auto base = std::filesystem::temp_directory_path() / ("wolf-overlay-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto assets = base / "assets", storage = base / "storage";
    wolf::atomic_write((assets / "Data/SC1.jpg").u8string(), "original");
    wolf::atomic_write((storage / "Data/SC1.jpg").u8string(), "new screenshot");
    wolf::atomic_write((storage / "save/preview.jpg").u8string(), "preview");
    wolf::Resources resources(assets.u8string(), storage.u8string());
    REQUIRE(resources.read_text("Data/SC1.jpg") == "new screenshot");
    REQUIRE(resources.read_text("Data/../Save/preview.jpg") == "preview");
    REQUIRE(resources.read_text("./Save/preview.jpg") == "preview");
    bool rejected = false; try { resources.path("Data/../../outside.jpg"); } catch (...) { rejected = true; }
    REQUIRE(rejected); wolf::Resources original(assets.u8string()); REQUIRE(original.read_text("Data/SC1.jpg") == "original");
    std::filesystem::remove_all(base);
}
void screenshot_commands_yield_until_frontend_handles_the_request() {
    const auto p = project({cmd(1000, {0}, 0, {"Save/preview.jpg"}), cmd(121, {2000000, 77, 0, 0})});
    wolf::Runtime rt(p, "storage"); rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.number(2000000) == 0); REQUIRE(rt.file_operations_pending());
    const auto operations = rt.take_file_operations(); REQUIRE(operations.size() == 1);
    REQUIRE(operations.at(0).at("operation") == 0); REQUIRE(operations.at(0).at("path") == "Save/preview.jpg");
    rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000000) == 77);
    wolf::Runtime unattended(p, "storage"); unattended.start_common(0); unattended.tick(); unattended.tick();
    REQUIRE(!unattended.error().empty()); REQUIRE(unattended.number(2000000) == 0);
}
void image_brightening_clamps_rgb_and_preserves_alpha() {
    const std::vector<unsigned char> original{128, 50, 200, 37, 0, 255, 100, 0};
    const auto tinted = wolf::adjust_image_color(original, {200, 50, 125});
    REQUIRE(tinted == std::vector<unsigned char>({255, 25, 250, 37, 0, 127, 125, 0}));
    REQUIRE(original.at(0) == 128);
}
void inline_sound_metadata_overrides_command_fields() {
    wolf::Runtime rt(project({cmd(140, {0x02000020, 0, 0, 0, 12, 50}, 0, {"se.ogg\r\n80\r\n100"}),
        cmd(140, {0x02000000, 0, 0, 0, 12, 50, 0}, 0, {"bgm.ogg\n77\n125\n300"})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty()); const auto audio = rt.take_audio();
    REQUIRE(audio.at(0).at("file") == "se.ogg"); REQUIRE(audio.at(0).at("volume") == 80); REQUIRE(audio.at(0).at("tempo") == 100);
    REQUIRE(audio.at(1).at("file") == "bgm.ogg"); REQUIRE(audio.at(1).at("loop_ms") == 300);
    REQUIRE(rt.number(9000095) == 77); REQUIRE(rt.number(9000096) == 125);
}
void optional_zero_se_field_preserves_playback_parameters() {
    const auto play = cmd(140, {0x02000020, 3, 0, 2, 90, 110, 0}, 0, {"se.ogg"});
    wolf::Runtime rt(project({play, cmd(180, {5})})); rt.start_common(0); rt.tick();
    REQUIRE(rt.error().empty()); REQUIRE(rt.take_audio().empty());
    for (int i = 0; i < 3; ++i) rt.tick();
    const auto audio = rt.take_audio(); REQUIRE(audio.size() == 1);
    REQUIRE(audio.at(0).at("file") == "se.ogg"); REQUIRE(audio.at(0).at("channel") == 2);
    REQUIRE(audio.at(0).at("volume") == 90); REQUIRE(audio.at(0).at("tempo") == 110);
    auto other = play; other["args"][6] = 1;
    wolf::Runtime unsupported(project({other})); unsupported.start_common(0); unsupported.tick();
    REQUIRE(!unsupported.error().empty());
}
void return_to_title_resets_game_state_and_keeps_saved_settings() {
    const auto temporary = std::filesystem::temp_directory_path();
    const auto base = temporary / ("wolf-title-reset-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto p = project({cmd(222, {123, 3, 2000090, 0}), cmd(121, {2000000, 77, 0, 0}),
        cmd(122, {3000000, 0}, 0, {"old game"}), cmd(121, {1100000000, 99, 0, 0}),
        cmd(150, {0, 1, 0, 1, 1, 1, 255, 20, 30, 100, 0}, 0, {"test.png"}),
        cmd(140, {0x02000000, 0, 0, 0, 90, 100, 0}, 0, {"music.ogg"}), cmd(180, {1}),
        cmd(174), cmd(121, {2000099, 999, 0, 0})});
    auto reader = p["common"]["events"][0]; reader["id"] = 1;
    reader["commands"] = Json::array({cmd(221, {2000005, 3, 2000090, 0})}); p["common"]["events"].push_back(reader);
    wolf::Runtime rt(p, base.u8string()); rt.start_common(0); rt.tick(); rt.take_audio();
    rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.frame() == 2);
    REQUIRE(rt.number(2000000) == 0); REQUIRE(rt.number(2000099) == 0); REQUIRE(rt.text(3000000).empty());
    REQUIRE(rt.number(1100000000) == 42); REQUIRE(rt.pictures().empty());
    REQUIRE(rt.snapshot().at("runs").empty()); REQUIRE(rt.snapshot().at("play_time_ticks") == 0);
    const auto audio = rt.take_audio(); REQUIRE(audio.size() == 3);
    for (const auto& event : audio) REQUIRE(event.at("operation") == "stop");
    rt.start_common(1); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000005) == 123);
    REQUIRE(std::filesystem::equivalent(base.parent_path(), temporary)); std::filesystem::remove_all(base);
}
void force_stop_message_unblocks_its_owner_without_reopening_text() {
    auto p = project({cmd(101, {}, 0, {"First message"}), cmd(121, {2000001, 77, 0, 0}), cmd(101, {}, 0, {"Second message"})});
    auto stopper = p["common"]["events"][0]; stopper["id"] = 1;
    stopper["commands"] = Json::array({cmd(105)}); p["common"]["events"].push_back(stopper);
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); REQUIRE(rt.number(9000012) == 1);
    rt.start_common(1); rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(9000012) == 0);
    REQUIRE(rt.message_state().at("done") == true); REQUIRE(rt.message().empty());
    rt.tick(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000001) == 77);
    REQUIRE(rt.message() == "Second message"); REQUIRE(rt.number(9000012) == 1);
}
void absent_label_falls_through_like_the_supplied_windows_engine() {
    wolf::Runtime rt(project({cmd(213, {}, 0, {"Debugskip"}), cmd(121, {2000001, 77, 0, 0}),
        cmd(213, {}, 0, {"Exists"}), cmd(121, {2000001, 999, 0, 0}), cmd(212, {}, 0, {"Exists"}), cmd(121, {2000002, 88, 0, 0})}));
    rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
    REQUIRE(rt.number(2000001) == 77); REQUIRE(rt.number(2000002) == 88);
    REQUIRE(rt.diagnostics().at("warnings").size() == 1);
}
void frontend_title_request_recovers_a_compatibility_stop() {
    wolf::Runtime rt(project({cmd(121, {2000001, 77, 0, 0}), cmd(98765)}));
    rt.start_common(0); rt.tick(); REQUIRE(!rt.error().empty());
    rt.return_to_title(); REQUIRE(rt.error().empty()); REQUIRE(rt.number(2000001) == 0);
    REQUIRE(rt.snapshot().at("runs").empty()); REQUIRE(rt.take_audio().size() == 3);
}
void picture_moves_preserve_the_original_anchor_like_windows() {
    for (const auto& anchors : std::vector<std::pair<int, int>>{{1, 0}, {1, 4}, {4, 1}}) {
        wolf::Runtime rt(project({cmd(150, {48 | (anchors.first << 12), 1, 0, 80, 40, 1, 255, 200, 180, 100, 0}, 0, {"<SQUARE>"}),
            cmd(150, {0x400f01 | (anchors.second << 12), 1, 0, 0, 0, -1000000, -1000000, 200, 180, -1000000, -1000000, 0, 16777216})}));
        rt.start_common(0); rt.tick(); REQUIRE(rt.error().empty());
        REQUIRE(rt.pictures().at("1").at("anchor") == anchors.first);
        REQUIRE(rt.pictures().at("1").at("x") == 200); REQUIRE(rt.pictures().at("1").at("y") == 180);
    }
}
void group_picture_zoom_matches_reference_geometry_and_preserves_queries() {
    const auto p = project({cmd(150, {0x1030, 10, 0, 80, 40, 1, 255, 200, 180, 100, 0}, 0, {"<SQUARE>"}),
        cmd(150, {0x1030, 11, 0, 80, 40, 1, 255, 300, 180, 100, 0}, 0, {"<SQUARE>"}),
        cmd(290, {64, 4, 10, 11, 200, 180, 150}), cmd(180, {99})});
    wolf::Runtime rt(p); rt.start_common(0); rt.tick(); rt.tick();
    const auto zoom = wolf::picture_group_zoom(rt.pictures().at("10"), 2);
    REQUIRE(zoom[2] == 1.25);
    wolf::Runtime restored(p); restored.restore(rt.snapshot());
    for (int i = 0; i < 4; ++i) restored.tick();
    for (const char* id : {"10", "11"}) {
        const auto& picture = restored.pictures().at(id);
        const auto effect = wolf::picture_group_zoom(picture, restored.frame());
        REQUIRE(effect[2] == 1.5); REQUIRE(picture.at("zoom") == 100);
        const double left = effect[0] + (picture.at("x").get<double>() - 40 - effect[0]) * effect[2];
        REQUIRE(left == (std::string(id) == "10" ? 140 : 290));
        REQUIRE(effect[1] + (180 - 20 - effect[1]) * effect[2] == 150);
    }
}
void game_clock_is_writable_and_survives_restore() {
    const auto p = project({cmd(180, {100})}); wolf::Runtime rt(p);
    rt.set_number(9000028, 9999999); rt.set_number(9000029, 7); rt.start_common(0); rt.tick();
    REQUIRE(rt.number(9000028) == 0); REQUIRE(rt.number(9000029) == 7); REQUIRE(rt.number(9000030) == 16);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot());
    for (int i = 0; i < 59; ++i) loaded.tick();
    REQUIRE(loaded.number(9000028) == 59); REQUIRE(loaded.number(9000029) == 8); REQUIRE(loaded.number(9000030) == 0);
}
void full_install_and_thin_update() {
    const auto temp = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto base = temp / ("wolf-port-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::weakly_canonical(base).parent_path() == temp);
    const auto bundle = base / "bundle", storage = base / "storage";
    wolf::atomic_save((bundle / "game.json").u8string(), project({cmd(180, {1})}));
    const std::string identity(64, 'a');
    wolf::atomic_save((bundle / "asset-manifest.json").u8string(), {{"format", "wolf-ios-assets"}, {"version", 1},
        {"id", identity}, {"files", {{{"path", "game.json"}, {"bytes", std::filesystem::file_size(bundle / "game.json")}}}}});
    std::size_t progress = 0;
    const auto installed = wolf::resolve_cached_game(bundle, storage, [&](std::size_t done, std::size_t) { progress = done; });
    REQUIRE(progress == 1); REQUIRE(std::filesystem::exists(installed / "game.json"));
    const auto reused = wolf::resolve_cached_game(base / "thin-build-without-assets", storage);
    REQUIRE(reused == installed);
    progress = 0;
    REQUIRE(wolf::resolve_cached_game(bundle, storage, [&](std::size_t, std::size_t) { ++progress; }) == installed);
    REQUIRE(progress == 0);
    std::filesystem::remove_all(base);
}
int main() {
    try {
        execution_and_waits(); branch_and_loop(); self_banks_and_snapshot(); unsupported_is_fatal(); atomic_overwrite(); named_database_and_bounds(); route_waits_and_persistence(); database_counts_and_absent_rows(); saved_variables_survive_restart(); literal_indirect_and_real_arithmetic(); common_event_string_arguments(); map_events_precede_parallel_common_events(); relative_common_calls(); string_conditions(); strings_and_resource_files(); picture_file_sources(); picture_flicker_and_input_replay(); delayed_picture_fade_and_sound(); choices_wait_and_restore(); picture_interpolation_survives_resume(); transfer_preserves_caller_and_changes_map(); picture_ranges_match_windows(); nested_condition_breaks_nearest_loop(); picture_hover_uses_original_dimensions(); message_reveal_waits_and_restores(); database_and_many_text_substitutions(); picture_delay_reset_splits_pending_range(); picture_move_preserves_graphic_and_same_values(); full_install_and_thin_update();
        delayed_text_picture_reveal_survives_restore(); delayed_moves_use_current_position_and_can_be_cancelled();
        game_clock_is_writable_and_survives_restore();
        pending_pictures_accept_effects_and_queries_like_windows();
        picture_effects_are_separate_from_query_values(); ranged_move_includes_existing_ids_when_first_is_missing();
        basic_and_mouse_input_permissions();
        automatic_basic_input_is_deferred_and_restored(); choice_mouse_selects_clicked_row_and_rejects_outside_click();
        delayed_se_dispatch_survives_restore();
        music_stop_preserves_kind_and_fade_duration();
        zero_sound_frequency_matches_windows_default();
        numeric_condition_literal_rhs_can_look_like_a_variable();
        arithmetic_ranges_match_windows_endpoints_and_aliasing(); arithmetic_range_random_draws_match_repeated_assignments();
        numeric_common_range_preserves_strings_and_other_events();
        picture_shake_matches_windows_samples_and_resume(); finite_picture_shake_returns_to_base_without_changing_queries();
        picture_show_retains_fields_and_uses_defaults_for_new_ids(); delayed_picture_show_retains_values_at_activation();
        full_game_save_survives_restart_and_shares_partial_variables(); missing_game_save_continues_and_parallel_load_is_rejected();
        saved_database_fields_use_the_slot_not_live_memory();
        game_load_discards_pending_work_and_rebases_picture_ages(); corrupted_full_save_does_not_replace_live_state();
        builtin_shapes_have_expected_gradients_and_transparency(); writable_resources_override_assets_without_general_parent_traversal();
        screenshot_commands_yield_until_frontend_handles_the_request();
        image_brightening_clamps_rgb_and_preserves_alpha();
        inline_sound_metadata_overrides_command_fields();
        optional_zero_se_field_preserves_playback_parameters();
        return_to_title_resets_game_state_and_keeps_saved_settings();
        force_stop_message_unblocks_its_owner_without_reopening_text();
        absent_label_falls_through_like_the_supplied_windows_engine(); frontend_title_request_recovers_a_compatibility_stop();
        picture_moves_preserve_the_original_anchor_like_windows();
        group_picture_zoom_matches_reference_geometry_and_preserves_queries();
        std::cout << "66 native runtime tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
