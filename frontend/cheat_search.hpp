#pragma once
#include "play_tools.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <set>

namespace play_tools {
enum class Comparison { Any, Equal, NotEqual, Greater, Less, Changed, Unchanged, Increased, Decreased };
struct SearchQuery { std::string name; std::optional<std::int32_t> value; Comparison comparison = Comparison::Equal; };
class CheatSearch {
public:
    std::map<int, std::int32_t> results;
    std::vector<std::map<int, std::int32_t>> history;
    bool started = false;
    static std::string lower(std::string value) {
        for (auto& ch : value) if (static_cast<unsigned char>(ch) < 128) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return value;
    }
    void scan(wolf::Runtime& runtime, const Cheats& cheats, const SearchQuery& query, bool refine) {
        if (refine && !started) throw std::runtime_error("Start a new search or capture a baseline first");
        const auto previous = results;
        std::set<int> candidates;
        if (refine) for (const auto& [ref, value] : previous) { (void)value; candidates.insert(ref); }
        else {
            for (const auto& [ref, name] : cheats.names) { (void)name; candidates.insert(ref); }
            for (int ref : runtime.numeric_variable_refs()) candidates.insert(ref);
            for (const auto& [ref, entry] : cheats.entries) { (void)entry; candidates.insert(ref); }
        }
        std::map<int, std::int32_t> found;
        const auto needle = lower(query.name);
        const bool id_search = !needle.empty() && std::all_of(needle.begin(), needle.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
        if (id_search && !refine) {
            int id=0;const auto read=std::from_chars(needle.data(),needle.data()+needle.size(),id);
            if (read.ec==std::errc{} && id>=0 && id<1000000) candidates.insert(2000000+id);
        }
        for (int ref : candidates) {
            const auto named = cheats.names.find(ref);
            const auto haystack = lower(cheats.label(ref) + " " + (named == cheats.names.end() ? "" : named->second) + " " + std::to_string(ref - 2000000));
            if (id_search ? std::to_string(ref - 2000000) != needle : !needle.empty() && haystack.find(needle) == std::string::npos) continue;
            const auto current = runtime.number(ref);
            bool matches = true;
            switch (query.comparison) {
            case Comparison::Any: break;
            case Comparison::Equal: matches = !query.value || current == *query.value; break;
            case Comparison::NotEqual: matches = !query.value || current != *query.value; break;
            case Comparison::Greater: matches = !query.value || current > *query.value; break;
            case Comparison::Less: matches = !query.value || current < *query.value; break;
            case Comparison::Changed: matches = !refine || current != previous.at(ref); break;
            case Comparison::Unchanged: matches = !refine || current == previous.at(ref); break;
            case Comparison::Increased: matches = !refine || current > previous.at(ref); break;
            case Comparison::Decreased: matches = !refine || current < previous.at(ref); break;
            }
            if (matches) found[ref] = current;
        }
        if (started) { if (history.size() == 12) history.erase(history.begin()); history.push_back(previous); }
        results = std::move(found); started = true;
    }
    void undo() { if (!history.empty()) { results = std::move(history.back()); history.pop_back(); } }
};
}
