#include "runtime.hpp"
#include "asset_cache.hpp"
#include "input_replay.hpp"
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
        cmd(150, {2, 5, 4, 2}), cmd(140, {33554464, 0, 0, 7, 80, 120}, 0, {"SE/test.ogg"}), cmd(180, {100})});
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
    const auto p = project({cmd(102, {19}, 0, {"first", "second", "third"}),
        cmd(401, {2}), cmd(121, {2000000, 10, 0, 0}, 1),
        cmd(401, {3}), cmd(121, {2000000, 20, 0, 0}, 1),
        cmd(401, {4}), cmd(121, {2000000, 30, 0, 0}, 1), cmd(499)});
    wolf::Runtime rt(p); rt.start_common(0);
    wolf::Input confirm; confirm.confirm = true;
    rt.tick(confirm); rt.tick(confirm); REQUIRE(rt.number(2000000) == 0); REQUIRE(rt.number(9000020) == 0);
    rt.tick(); wolf::Input down; down.down = true; rt.tick(down); REQUIRE(rt.number(9000020) == 1);
    wolf::Runtime loaded(p); loaded.restore(rt.snapshot()); loaded.tick(); loaded.tick(confirm);
    REQUIRE(loaded.number(2000000) == 0); loaded.tick(); REQUIRE(loaded.number(2000000) == 20);
    REQUIRE(loaded.number(9000020) == 1); loaded.tick(); REQUIRE(loaded.number(9000020) == -1); REQUIRE(loaded.error().empty());
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
        execution_and_waits(); branch_and_loop(); self_banks_and_snapshot(); unsupported_is_fatal(); atomic_overwrite(); named_database_and_bounds(); route_waits_and_persistence(); database_counts_and_absent_rows(); saved_variables_survive_restart(); literal_indirect_and_real_arithmetic(); common_event_string_arguments(); map_events_precede_parallel_common_events(); relative_common_calls(); string_conditions(); strings_and_resource_files(); picture_file_sources(); picture_flicker_and_input_replay(); delayed_picture_fade_and_sound(); choices_wait_and_restore(); picture_interpolation_survives_resume(); full_install_and_thin_update();
        std::cout << "21 native runtime tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
