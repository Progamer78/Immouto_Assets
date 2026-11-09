#include "runtime.hpp"
#include "asset_cache.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using wolf::Json;
#define REQUIRE(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (false)

Json cmd(int code, Json args = Json::array(), int indent = 0, Json strings = Json::array()) {
    return {{"code", code}, {"args", args}, {"indent", indent}, {"strings", strings}, {"offset", 0}};
}
Json project(Json commands) {
    Json types = Json::array();
    for (int i = 0; i < 8; ++i) types.push_back({{"rows", Json::array()}});
    types[0]["rows"] = {{"MapData/Test.mps"}}; types[7]["rows"] = {{0, 0, 0}};
    Json p = {{"format", "wolf-ios-ir"}, {"version", 1}};
    p["game"] = {{"title", "fixture"}, {"width", 1024}, {"height", 576}, {"fps", 60}};
    p["common"]["events"] = Json::array({{{"id", 0}, {"name", "test"}, {"mode", 0},
        {"return_id", 1600000}, {"commands", commands}}});
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
        execution_and_waits(); branch_and_loop(); self_banks_and_snapshot(); unsupported_is_fatal(); atomic_overwrite(); named_database_and_bounds(); full_install_and_thin_update();
        std::cout << "7 native runtime tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
