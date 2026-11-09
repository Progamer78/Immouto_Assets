#include "runtime.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace wolf {
bool Runtime::string_reference(int ref) {
    if (ref >= 1000000000) return cell(ref).is_string();
    return (ref >= 3000000 && ref < 4000000) || (ref >= 9900000 && ref < 10000000) ||
        (ref >= 1600005 && ref <= 1600009) || (ref >= 15000000 && ref < 16000000 && ref % 100 >= 5 && ref % 100 <= 9);
}

std::string Runtime::save_name(int identifier, Context context) {
    std::string name;
    if (string_reference(identifier)) name = text(identifier, context);
    else {
        const int slot = number(identifier, context);
        if (slot < 0 || slot > 99999) fail("Invalid native save slot");
        std::ostringstream stream; stream << "save/savedata" << std::setfill('0') << std::setw(2) << slot << ".sav";
        name = stream.str();
    }
    // Windows save names are case-insensitive; preserve that on iOS's filesystem.
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return std::tolower(ch); });
    const auto relative = std::filesystem::u8path(name);
    if (name.empty() || relative.is_absolute() || name.find_first_of("\\:%*?\"<>|") != std::string::npos || name.find("//") != std::string::npos)
        fail("Invalid native save filename");
    for (const auto& part : relative) if (part == "." || part == "..") fail("Native save filename escapes storage");
    return name;
}

Json& Runtime::save_slot(const std::string& name) {
    const auto path = std::filesystem::u8path(save_root_) / std::filesystem::u8path(name + ".json");
    auto found = save_slots_.find(name);
    if (found == save_slots_.end()) {
        Json slot;
        if (!save_root_.empty() && std::filesystem::exists(path)) {
            if (std::filesystem::is_symlink(path) || std::filesystem::file_size(path) > 64 * 1024 * 1024)
                fail("Invalid native saved-variable file");
            slot = load_json(path.u8string());
            if (slot.at("format") != "wolf-native-variables" || slot.at("version") != 1 || slot.at("project_id") != project_id_)
                fail("Incompatible native saved-variable file");
        }
        // Partial variables and a native game-state payload share each slot.
        // The JSON envelope cannot be mistaken for a Windows save file.
        if (save_slots_.size() >= 64) {
            flush_saves(); save_slots_.clear();
        }
        found = save_slots_.emplace(name, std::move(slot)).first;
    }
    return found->second;
}

void Runtime::saved_variable(const Json& cmd, Context context) {
    if (cmd.at("args").size() != 4 || (cmd.at("args").at(3) != 0 && cmd.at("args").at(3) != 1))
        fail("Unsupported saved-variable command layout");
    const auto name = save_name(cmd.at("args").at(1), context);
    auto& slot = save_slot(name);
    const int variable = cmd.at("args").at(3) == 1 ? number(cmd.at("args").at(2), context) : cmd.at("args").at(2).get<int>();
    const int live = cmd.at("args").at(0);
    if (cmd.at("code") == 221) {
        if (variable == 9000024) { set_number(live, !slot.is_null(), context); return; }
        if (variable >= 1000000000) {
            const char* bank = nullptr; int offset = 0;
            if (variable < 1100000000) { bank = "user"; offset = variable - 1000000000; }
            else if (variable < 1200000000) { bank = "mutable"; offset = variable - 1100000000; }
            else if (variable >= 1300000000 && variable < 1400000000) { bank = "system"; offset = variable - 1300000000; }
            if (!bank) fail("Invalid saved database reference");
            const Json* field = nullptr;
            if (!slot.is_null() && slot.contains("game"))
                field = &slot.at("game").at("databases").at(bank).at("types").at(offset / 1000000).at("rows")
                    .at((offset % 1000000) / 100).at(offset % 100);
            if (string_reference(live)) set_text(live, field ? field->get<std::string>() : "", context);
            else set_number(live, field ? field->get<int>() : 0, context);
            return;
        }
        const std::string key = std::to_string(variable);
        if (string_reference(live)) {
            const std::string value = slot.is_null() ? "" : slot.at("strings").value(key, std::string{});
            set_text(live, value, context);
        } else {
            int value = slot.is_null() ? 0 : slot.at("numbers").value(key, 0);
            if (!slot.is_null() && slot.contains("game") && (variable == 9000029 || variable == 9000030)) {
                const auto ticks = slot.at("game").at("play_time_ticks").get<std::uint64_t>();
                const int fps = project_->at("game").at("fps");
                value = variable == 9000029 ? ticks / fps : (ticks % fps) * 1000 / fps;
            }
            set_number(live, value, context);
        }
    } else {
        if (save_root_.empty()) fail("Saved-variable writes require a persistent storage directory");
        if (variable < 2000000 || variable >= 10000000) fail("Unsupported variable bank in partial save");
        if (slot.is_null()) slot = {{"format", "wolf-native-variables"}, {"version", 1}, {"project_id", project_id_},
            {"numbers", Json::object()}, {"strings", Json::object()}};
        const std::string key = std::to_string(variable);
        if (string_reference(variable)) {
            if (!string_reference(live)) fail("Numeric source for a saved string");
            slot["strings"][key] = text(live, context);
        } else {
            if (string_reference(live)) fail("String source for a saved number");
            slot["numbers"][key] = number(live, context);
        }
        dirty_saves_.insert(name);
    }
}

void Runtime::flush_saves() {
    for (const auto& name : dirty_saves_) {
        const auto path = std::filesystem::u8path(save_root_) / std::filesystem::u8path(name + ".json");
        atomic_save(path.u8string(), save_slots_.at(name));
    }
    dirty_saves_.clear();
}
}
