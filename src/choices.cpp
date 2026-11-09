#include "runtime.hpp"
#include "text.hpp"
#include <algorithm>

namespace wolf {
void Runtime::prepare_choice_layout() {
    if (asset_root_.empty()) return; // Core-only synthetic fixtures need no fonts.
    if (!text_rasterizer_) text_rasterizer_ = std::make_shared<TextRasterizer>(asset_root_, project_.at("game"),
        databases_.at("system").at("types").at(12).at("rows"));
    const Json style = {{"size", number(9000008)}, {"shadow", number(9000021)}, {"speed", 0},
        {"antialias", project_.at("game").at("flags").at(14)}};
    const int border = 6 + std::max(0, number(9000021));
    const int pad_x = std::max(0, number(9000026)), pad_y = std::max(0, number(9000027));
    const int gap = project_.at("game").at("flags").at(11);
    int width = 0, height = 0;
    Json rows = Json::array();
    for (const auto& text : choice_.at("strings")) {
        const auto size = text_rasterizer_->measure(text, style);
        const int row_height = std::max(1, size.second);
        width = std::max(width, size.first);
        rows.push_back({{"top", height}, {"height", row_height + gap}, {"content_height", row_height}}); height += row_height + gap;
    }
    width += pad_x * 2; height += pad_y * 2 - gap;
    const int center = choice_.at("x").get<int>() == -1 ? project_.at("game").at("width").get<int>() / 2 : choice_.at("x").get<int>();
    const int top = choice_.at("y").get<int>() == -1 ? (project_.at("game").at("height").get<int>() - height) / 2 : choice_.at("y").get<int>();
    choice_["layout"] = {{"x", center - width / 2}, {"y", top}, {"width", width}, {"height", height},
        {"pad_x", pad_x}, {"pad_y", pad_y}, {"border", border}, {"rows", rows}, {"style", style},
        {"show_window", number(9000014) != 0}, {"cursor_blend", number(9000022)},
        {"skin", databases_.at("system").at("types").at(10).at("rows").at(1).at(0)},
        {"cursor", databases_.at("system").at("types").at(10).at("rows").at(2).at(0)}};
}

Json Runtime::choice_pictures() const {
    Json result = Json::object();
    if (choice_.empty() || choice_.at("done") == true || !choice_.contains("layout")) return result;
    const auto& layout = choice_.at("layout");
    const int x = layout.at("x"), y = layout.at("y"), width = layout.at("width"), height = layout.at("height");
    auto picture = [&](int id, int kind, const std::string& content, int px, int py, int w, int h) {
        result[std::to_string(id)] = {{"id", id}, {"kind", kind}, {"content", content}, {"x", px}, {"y", py},
            {"div_x", w}, {"div_y", h}, {"pattern", 1}, {"opacity", 255}, {"zoom", 100}, {"zoom_y", 100},
            {"angle", 0}, {"anchor", 0}, {"blend", 0}};
    };
    if (layout.at("show_window") == true && !layout.at("skin").get<std::string>().empty()) picture(0, 3, layout.at("skin"), x, y, width, height);
    const int selected = choice_.at("selected");
    if (selected >= 0 && selected < static_cast<int>(layout.at("rows").size()) && !layout.at("cursor").get<std::string>().empty()) {
        const auto& row = layout.at("rows").at(selected);
        picture(1, 3, layout.at("cursor"), x + layout.at("pad_x").get<int>() - 4, y + layout.at("pad_y").get<int>() + row.at("top").get<int>() - 2,
            width - 2 * layout.at("pad_x").get<int>() + 8, row.at("content_height").get<int>() + 4);
        result["1"]["blend"] = layout.at("cursor_blend");
    }
    for (std::size_t i = 0; i < choice_.at("strings").size(); ++i) {
        picture(2 + static_cast<int>(i), 2, choice_.at("strings").at(i), x + layout.at("pad_x").get<int>(),
            y + layout.at("pad_y").get<int>() + layout.at("rows").at(i).at("top").get<int>(), 1, 1);
        result[std::to_string(2 + i)]["text_style"] = layout.at("style");
    }
    return result;
}
}
