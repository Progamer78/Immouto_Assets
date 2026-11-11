#include "runtime.hpp"
#include <filesystem>

namespace wolf {
int Runtime::import_pc_progress(const Json& progress) {
    if (save_root_.empty()) fail("Import requires persistent storage");
    if (progress.at("format")!="wolf-pc-progress" || progress.at("version")!=1 ||
        progress.at("game_title")!="Living with Sister: Monochrome Fantasy" || project_->at("game").at("width")!=1024)
        fail("This converted save does not match the supplied game");
    Runtime candidate(project_,save_root_,asset_root_,resource_root_,project_id_);
    auto state=candidate.snapshot();
    // The imported payload contains persistent game progress, not Windows UI
    // internals. Keep the phone's established font/input presentation defaults.
    for (const auto& [ref,value]:numbers_) if (ref>=9000000 && ref<9000129) state["numbers"][std::to_string(ref)]=value;
    if (state["numbers"].value("9000008",0)<1) state["numbers"]["9000008"]=20;
    state["numbers"]["9000012"]=0;state["numbers"]["9000020"]=-1;
    const int map=progress.at("map"); if (!candidate.map_data(map)) fail("PC save map is absent from this game version");
    state["map"]=map;state["play_time_ticks"]=progress.at("play_time_ticks").get<std::uint64_t>();
    const auto& types=progress.at("mutable_types");auto& target=state["databases"]["mutable"]["types"];
    if (types.size()!=target.size()) fail("PC save database types differ from the installed game");
    for (std::size_t type=0;type<types.size();++type) {
        const auto& source=types.at(type);const auto& rows=source.at("rows");const auto& original=target.at(type);
        if (source.at("positions")!=original.at("positions") || rows.size()!=original.at("rows").size())
            fail("PC save database layout differs from the installed game");
        for (std::size_t row=0;row<rows.size();++row) {
            const auto& expected=original.at("rows").at(row);const auto& incoming=rows.at(row);
            if (!incoming.is_array() || incoming.size()!=expected.size()) fail("PC save database fields differ from the installed game");
            for (std::size_t field=0;field<incoming.size();++field) {
                if (expected.at(field).is_string()) { (void)incoming.at(field).get<std::string>(); }
                else { (void)incoming.at(field).get<std::int32_t>(); }
            }
        }
        target[type]["rows"]=rows;
    }
    if (!progress.at("numbers").is_object() || !progress.at("strings").is_object()) fail("Invalid converted PC variables");
    for (const auto& [key,value]:progress.at("numbers").items()) {
        std::size_t used=0;const auto ref=std::stoull(key,&used);
        const bool global=ref>=2000000 && ref<3000000, common=ref>=15000000 && ref<15100000;
        const bool map_self=(ref>>48)==1 && (ref&0xffffff)>=1000000 && (ref&0xffffff)<1100000;
        if (used!=key.size() || !(global || common || map_self)) fail("Invalid converted PC numeric reference");
        state["numbers"][key]=value.get<std::int32_t>();
    }
    for (const auto& [key,value]:progress.at("strings").items()) {
        std::size_t used=0;const auto ref=std::stoull(key,&used);
        const bool global=ref>=3000000 && ref<4000000;
        const bool common=ref>=15000000 && ref<15100000 && ref%100>=5 && ref%100<=9;
        if (used!=key.size() || !(global || common)) fail("Invalid converted PC string reference");
        state["strings"][key]=value.get<std::string>();
    }
    // The supplied game's Windows Load sets this through a post-save partial
    // variable write. It is not part of the older complete binary snapshot.
    state["numbers"]["2000302"]=2;state["numbers"]["9000025"]=1;
    const int day=state["numbers"].value("2000160",0);
    state["strings"]["3000040"]="Imported PC save - Day "+std::to_string(day+1);
    int slot=2; // Slot 1 is the original game's automatic save.
    for (;slot<=32;++slot) {
        const auto name="save/savedata"+(slot<10?std::string("0"):std::string{})+std::to_string(slot)+".sav";
        if (save_slots_.count(name) && !save_slots_.at(name).is_null()) continue;
        if (!std::filesystem::exists(std::filesystem::u8path(save_root_)/(name+".json"))) break;
    }
    if (slot>32) fail("All normal save slots are occupied; free a slot before importing");
    const auto name="save/savedata"+(slot<10?std::string("0"):std::string{})+std::to_string(slot)+".sav";
    state["strings"]["3000005"]="../save/pc-import-preview.jpg";
    candidate.restore(state); // Validate everything before any slot is written.
    const Json envelope={{"format","wolf-native-variables"},{"version",1},{"project_id",project_id_},
        {"numbers",state.at("numbers")},{"strings",state.at("strings")},{"game",state}};
    atomic_save((std::filesystem::u8path(save_root_)/(name+".json")).u8string(),envelope);
    save_slots_.erase(name); resources_->invalidate();
    return slot;
}
}
