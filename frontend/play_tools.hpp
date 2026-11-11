#pragma once
#include "runtime.hpp"
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace play_tools {
inline std::string literal_text(const std::string& text) {
    std::string result;for(char ch:text) {if(ch=='\\')result+='\\';result+=ch;}return result;
}
struct Cursor { float x = 512, y = 288; };
class SaveStates {
    std::filesystem::path directory_;
public:
    explicit SaveStates(const std::filesystem::path& storage) : directory_(storage / "states") {}
    std::filesystem::path path(int slot) const {
        if (slot < 1 || slot > 3) throw std::runtime_error("Invalid save-state slot");
        return directory_ / ("slot" + std::to_string(slot) + ".json");
    }
    bool exists(int slot) const { return std::filesystem::exists(path(slot)); }
    std::string label(int slot) const {
        const auto prefix = "Slot " + std::to_string(slot);
        if (!exists(slot)) return prefix + " (empty)";
        try { return prefix + " - " + wolf::load_json(path(slot).u8string()).value("saved_at", "saved"); }
        catch (...) { return prefix + " (unreadable)"; }
    }
    void save(int slot, const wolf::Runtime& runtime, Cursor cursor) const {
        const auto now = std::time(nullptr); std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::ostringstream when; when << std::put_time(&utc, "%Y-%m-%d %H:%M UTC");
        wolf::atomic_save(path(slot).u8string(), {{"format", "wolf-manual-state"}, {"version", 1},
            {"saved_at", when.str()}, {"cursor", {cursor.x, cursor.y}}, {"runtime", runtime.snapshot()}});
    }
    Cursor load(int slot, wolf::Runtime& runtime) const {
        const auto saved = wolf::load_json(path(slot).u8string());
        if (saved.at("format") != "wolf-manual-state" || saved.at("version") != 1)
            throw std::runtime_error("Incompatible save state");
        Cursor cursor{saved.at("cursor").at(0), saved.at("cursor").at(1)};
        if (!std::isfinite(cursor.x) || !std::isfinite(cursor.y) || cursor.x < 0 || cursor.x > 1024 || cursor.y < 0 || cursor.y > 576)
            throw std::runtime_error("Invalid save-state cursor");
        runtime.restore(saved.at("runtime")); // Candidate validation leaves a failed load harmless.
        return cursor;
    }
};

struct Variable { int ref; const char* name; int maximum = 0; };
inline const std::array<Variable, 4> quick_variables{{
    {2000007, "Money", 0}, {2000013, "AP", 2000014},
    {2000015, "Vigor", 2000016}, {2000029, "Battle score", 2000028}}};
inline std::string variable_name(int ref) {
    for (const auto& variable : quick_variables) if (ref == variable.ref) return variable.name;
    return "Variable " + std::to_string(ref - 2000000);
}
struct CheatEntry { bool pinned = false, frozen = false; std::int32_t value = 0; std::string name; };
class Cheats {
    std::filesystem::path path_;
public:
    bool enabled = false, show_pins = false;
    std::map<int, CheatEntry> entries;
    std::map<int, std::string> names;
    explicit Cheats(const std::filesystem::path& storage) : path_(storage / "cheats.json") {
        for (const auto& variable : quick_variables) entries[variable.ref].pinned = true;
        if (!std::filesystem::exists(path_)) return;
        const auto data = wolf::load_json(path_.u8string());
        if (data.at("format") != "wolf-cheats" || (data.at("version") != 1 && data.at("version") != 2)) throw std::runtime_error("Incompatible cheat settings");
        std::map<int, CheatEntry> loaded;
        if (data.at("entries").size() > 256) throw std::runtime_error("Too many pinned or frozen variables");
        for (const auto& entry : data.at("entries")) {
            const int ref = entry.at("ref"); validate_ref(ref);
            loaded[ref] = {entry.at("pinned"), entry.at("frozen"), entry.at("value").get<std::int32_t>(), entry.value("name", "")};
        }
        enabled = data.at("enabled"); show_pins = data.value("show_pins", false); entries = std::move(loaded);
    }
    void catalog(const wolf::Json& project) {
        const auto& types = project.at("databases").at("system").at("types");
        if (types.size() <= 14 || !types.at(14).contains("schema")) return;
        const auto& labels = types.at(14).at("schema").at("names");
        for (std::size_t i = 0; i < labels.size(); ++i) names[2000000 + static_cast<int>(i)] = labels.at(i).get<std::string>();
    }
    std::string label(int ref) const {
        const auto entry = entries.find(ref);
        if (entry != entries.end() && !entry->second.name.empty()) return entry->second.name;
        for (const auto& variable : quick_variables) if (variable.ref == ref) return variable.name;
        const auto name = names.find(ref);
        return name != names.end() && !name->second.empty() ? name->second : variable_name(ref);
    }
    void rename(int ref, std::string name) { validate_ref(ref); if (name.size() > 160) throw std::runtime_error("Name is too long"); entries[ref].name = std::move(name); }
    static void validate_ref(int ref) {
        if (ref < 2000000 || ref >= 3000000) throw std::runtime_error("Variable number must be between 0 and 999999");
    }
    void apply(wolf::Runtime& runtime) const {
        std::map<int, std::int32_t> frozen;
        if (enabled) for (const auto& [ref, entry] : entries) if (entry.frozen) frozen[ref] = entry.value;
        runtime.set_number_freezes(frozen);
    }
    void persist() const {
        wolf::Json saved = wolf::Json::array();
        for (const auto& [ref, entry] : entries) if (entry.pinned || entry.frozen || !entry.name.empty())
            saved.push_back({{"ref", ref}, {"pinned", entry.pinned}, {"frozen", entry.frozen}, {"value", entry.value}, {"name", entry.name}});
        wolf::atomic_save(path_.u8string(), {{"format", "wolf-cheats"}, {"version", 2}, {"enabled", enabled}, {"show_pins", show_pins}, {"entries", saved}});
    }
    void edit(wolf::Runtime& runtime, int ref, std::int32_t value) {
        validate_ref(ref);
        auto& entry = entries[ref]; entry.value = value;
        apply(runtime); runtime.set_number(ref, value);
    }
    void freeze(wolf::Runtime& runtime, int ref, bool frozen) {
        validate_ref(ref);
        auto& entry = entries[ref]; entry.value = runtime.number(ref); entry.frozen = frozen; apply(runtime);
    }
};
}
