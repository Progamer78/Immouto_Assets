#include "runtime.hpp"
#include "text.hpp"
#include <algorithm>

namespace wolf {
void Runtime::picture_query(const Json& command, Context context) {
    const auto& args = command.at("args");
    const int options = args.at(1);
    if (args.size() != 4 || (options & ~0x4f11)) fail("Unsupported picture-query layout");
    const int destination = options & 16 ? number(args.at(0), context) : args.at(0).get<int>();
    const auto id = std::to_string(number(args.at(2), context));
    const int field = args.at(3);
    if (field < 0 || field > 12) fail("Unsupported picture query field " + std::to_string(field));
    int result = 0;
    if (field == 9) result = pictures_.contains(id) ? 1 : 0;
    else if (!pictures_.contains(id)) {
        result = -1; // Verified against Game.exe for every field 0..12 except existence.
    } else {
        const auto& picture = pictures_.at(id);
        switch (field) {
        case 0: result = picture.at("x"); break;
        case 1: result = picture.at("y"); break;
        case 4: result = picture.at("pattern"); break;
        case 5: result = picture.at("opacity"); break;
        case 6: result = picture.at("angle"); break;
        case 10:
            if (picture.at("kind") != 2) result = -1;
            else if (picture.contains("text_reveal")) result = picture.at("text_style").at("visible_chars").get<std::size_t>() == picture.at("text_reveal").at("times").size();
            else if (picture.at("text_style").at("speed") == 0) result = 1;
            else fail("Timed text-picture queries are not implemented");
            break;
        case 11: result = picture.at("zoom"); break;
        case 12: result = picture.at("zoom_y"); break;
        case 2: case 3: case 8: {
            const int kind = picture.at("kind");
            int width = 0, height = 0;
            if (kind == 0 || kind == 1) {
                const auto dimensions = resources_->image_size("Data/" + picture.at("content").get<std::string>());
                const int div_x = picture.at("div_x"), div_y = picture.at("div_y");
                if (div_x < 1 || div_y < 1) fail("Invalid picture divisions");
                width = dimensions.first / div_x; height = dimensions.second / div_y;
            } else if (kind == 3 || kind == 4) { width = picture.at("div_x"); height = picture.at("div_y"); }
            else if (kind == 2) {
                if (!text_rasterizer_) text_rasterizer_ = std::make_shared<TextRasterizer>(asset_root_, project_.at("game"),
                    databases_.at("system").at("types").at(12).at("rows"));
                const auto dimensions = text_rasterizer_->measure(picture.at("content"), picture.at("text_style"));
                width = dimensions.first; height = dimensions.second;
            } else fail("Unsupported picture type for dimensions");
            if (field == 2) result = width;
            else if (field == 3) result = height;
            else {
                if (picture.at("angle") != 0 || picture.at("zoom").get<int>() <= 0 || picture.at("zoom_y").get<int>() <= 0) fail("Rotated/flipped picture hit tests are not implemented");
                const double zoom = picture.at("zoom").get<double>() / 100;
                const double w = width * zoom, h = height * picture.at("zoom_y").get<double>() / 100;
                double x = picture.at("x"), y = picture.at("y");
                switch (picture.at("anchor").get<int>()) {
                case 0: break;
                case 1: x -= w / 2; y -= h / 2; break;
                case 2: y -= h; break;
                case 3: x -= w; break;
                case 4: x -= w; y -= h; break;
                case 5: x -= w / 2; break;
                case 6: x -= w / 2; y -= h; break;
                default: fail("Unsupported picture hit-test anchor");
                }
                result = input_.mouse_x >= x && input_.mouse_x < x + w && input_.mouse_y >= y && input_.mouse_y < y + h;
            }
            break;
        }
        default: fail("Unsupported picture query field " + std::to_string(field));
        }
    }
    result = assign(number(destination, context), result, (options >> 8) & 15);
    if (options & 1) result = std::clamp(result, -999999, 999999);
    set_number(destination, result, context);
}
}
