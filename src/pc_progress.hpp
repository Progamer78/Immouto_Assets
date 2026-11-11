#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace wolf {
inline std::string pc_plain_text(const std::string& input) {
    // Windows stores already-expanded display formatting as length-prefixed
    // control records. Cached labels can retain their text without feeding
    // those binary records to the native font rasterizer. Event text loaded
    // again from the original resources keeps its original formatting.
    std::string result;
    for (std::size_t i=0;i<input.size();) {
        const auto ch=static_cast<unsigned char>(input[i]);
        if (ch==1) {
            if (i+1>=input.size()) throw std::runtime_error("Truncated PC string formatting");
            const auto length=static_cast<unsigned char>(input[i+1]);
            if (!length || i+1+length>input.size()) throw std::runtime_error("Invalid PC string formatting");
            i+=1+length;
        } else {if(ch>=32 || ch=='\n' || ch=='\r' || ch=='\t')result+=input[i];++i;}
    }
    return result;
}
// Adapter for WolfSave's read-only, ordered dump. No game resources are needed
// by the Windows converter; the installed app validates the database schema.
inline nlohmann::json convert_pc_dump(const nlohmann::ordered_json& dump) {
    using J=nlohmann::json;
    auto section=[&](const std::string& suffix)->const nlohmann::ordered_json& {
        for (auto it=dump.begin();it!=dump.end();++it)
            if (it.key().size()>=suffix.size() && it.key().compare(it.key().size()-suffix.size(),suffix.size(),suffix)==0) return it.value();
        throw std::runtime_error("PC save section is missing: "+suffix);
    };
    const auto& header=section("Header");
    const std::string title=header.at("Game Name").at("value");
    if (title!="Living with Sister: Monochrome Fantasy") throw std::runtime_error("This save belongs to another game");
    const auto& part=section("SavePart3");
    auto field=[&](int index)->const nlohmann::ordered_json& {return part.at("var_#"+std::to_string(index)).at("value");};
    J numbers=J::object(),strings=J::object();
    auto add_number=[&](std::uint64_t ref,const nlohmann::ordered_json& value) {
        const auto raw=value.get<std::uint32_t>();
        const auto signed_value=raw<=INT32_MAX?static_cast<std::int64_t>(raw):static_cast<std::int64_t>(raw)-0x100000000LL;
        if (signed_value) numbers[std::to_string(ref)]=signed_value;
    };
    auto add_string=[&](std::uint64_t ref,const nlohmann::ordered_json& value) {
        const auto text=pc_plain_text(value.get<std::string>());if (!text.empty()) strings[std::to_string(ref)]=text;
    };
    std::size_t event_index=0,value_index=0;
    const auto maps=field(1).get<unsigned>();if (maps>10000 || field(2).size()!=maps) throw std::runtime_error("Invalid PC map variables");
    for (unsigned map=0;map<maps;++map) {
        const auto events=field(2).at(map).get<unsigned>();if (events>10000) throw std::runtime_error("Invalid PC event count");
        for (unsigned event=0;event<events;++event) {
            const auto count=field(3).at(event_index++).get<unsigned>();if (count>10) throw std::runtime_error("Unsupported PC self-variable count");
            for (unsigned slot=0;slot<count;++slot) add_number((1ULL<<48)|(static_cast<std::uint64_t>(map)<<24)|(1000000+event*10+slot),field(4).at(value_index++));
        }
    }
    if (event_index!=field(3).size() || value_index!=field(4).size()) throw std::runtime_error("Inconsistent PC map variables");
    value_index=0;const auto banks=field(5).get<unsigned>();if (banks>10 || field(6).size()!=banks) throw std::runtime_error("Unsupported PC variable banks");
    for (unsigned bank=0;bank<banks;++bank) {
        const auto count=field(6).at(bank).get<unsigned>();if (count>100000) throw std::runtime_error("Invalid PC variable count");
        for (unsigned slot=0;slot<count;++slot) add_number(2000000+bank*100000+slot,field(7).at(value_index++));
    }
    if (value_index!=field(7).size() || field(8)!=field(9).size()) throw std::runtime_error("Inconsistent PC global variables");
    for (std::size_t i=0;i<field(9).size();++i) add_string(3000000+i,field(9).at(i));
    value_index=0;
    if (field(10)!=field(11).size()) throw std::runtime_error("Inconsistent PC common variables");
    for (std::size_t common=0;common<field(11).size();++common) {
        const auto count=field(11).at(common).get<unsigned>();if (count>100) throw std::runtime_error("Unsupported PC common-variable count");
        for (unsigned slot=0;slot<count;++slot) add_number(15000000+common*100+slot,field(12).at(value_index++));
    }
    if (value_index!=field(12).size() || field(13)!=field(14).size()) throw std::runtime_error("Inconsistent PC common variables");
    value_index=0;
    for (std::size_t common=0;common<field(14).size();++common) {
        const auto count=field(14).at(common).get<unsigned>();if (count>5) throw std::runtime_error("Unsupported PC common strings");
        for (unsigned slot=0;slot<count;++slot) add_string(15000005+common*100+slot,field(15).at(value_index++));
    }
    if (value_index!=field(15).size()) throw std::runtime_error("Inconsistent PC common strings");
    J types=J::array();
    for (const auto& type:section("VariableDatabase")) {
        if (!type.is_object() || !type.contains("TypeData Count")) continue;
        J rows=J::array();
        for (const auto& entry:type) {
            if (entry.is_null()) {rows.push_back(J::array());continue;}
            if (!entry.is_object() || entry.contains("value")) continue;
            J row=J::array();for (const auto& field:entry) {
                const auto& value=field.at("value");row.push_back(value.is_string()?J(pc_plain_text(value.get<std::string>())):J(value));
            }rows.push_back(std::move(row));
        }
        if (rows.size()!=type.at("TypeData Count").at("value")) throw std::runtime_error("Inconsistent PC database rows at type "+std::to_string(types.size())+": "+std::to_string(rows.size()));
        types.push_back({{"positions",type.contains("Type Configuration")?J(type.at("Type Configuration").at("value")):J::array()}, {"rows",rows}});
    }
    const auto& db=section("VariableDatabase");
    if (types.size()!=db.at("Number of Types").at("value")) throw std::runtime_error("Inconsistent PC database types");
    return {{"format","wolf-pc-progress"},{"version",1},{"game_title",title},
        {"map",section("SavePart1").at("var_#0").at("value")},
        {"play_time_ticks",section("SavePart2").at("var_#2").at("value")},
        {"numbers",numbers},{"strings",strings},{"mutable_types",types}};
}
}
