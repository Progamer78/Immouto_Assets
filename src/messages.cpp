#include "runtime.hpp"
#include <algorithm>
#include <cmath>

namespace wolf {
void Runtime::show_message(Run& run, const Json& command) {
    auto& frame = run.stack.back();
    if (!command.at("args").empty() || command.at("strings").size() != 1) fail("Unsupported message-command layout");
    if (message_state_.empty()) {
        auto content = expand(text(9900026) + command.at("strings").at(0).get<std::string>() + text(9900027), frame.context);
        if (!content.empty() && content.front() == '@') {
            const auto end = content.find('\n');
            if (end != std::string::npos && end > 1 && content.substr(1, end - 1).find_first_not_of("0123456789\r") == std::string::npos) {
                set_number(9000000, std::stoi(content.substr(1, end - 1))); content.erase(0, end + 1);
            }
        }
        int count = 0; bool instant = false, automatic = false;
        std::string drawing;
        for (std::size_t i = 0; i < content.size();) {
            if (content.compare(i, 3, "<L>") == 0 || content.compare(i, 3, "<C>") == 0 || content.compare(i, 3, "<R>") == 0) {
                drawing.append(content, i, 3); i += 3; continue;
            }
            if (content[i] == '\\' && i + 1 < content.size()) {
                const auto start = i;
                const char code = content[++i];
                if (code == '>' || code == '<') {
                    if (count != 0) fail("Mid-message reveal-speed controls are not implemented");
                    instant = code == '>'; ++i; continue;
                }
                if (code == '^') { automatic = true; ++i; continue; }
                if (code == 'E' || code == 'N') { drawing.append(content, start, 2); ++i; continue; }
                if (code == '\\') { drawing.append(content, start, 2); ++count; ++i; continue; }
                if (content.compare(i, 2, "A+") == 0 || content.compare(i, 2, "A-") == 0) { drawing.append(content, start, 3); i += 2; continue; }
                const auto open = content.find('[', i), close = open == std::string::npos ? std::string::npos : content.find(']', open);
                if (open == std::string::npos || close == std::string::npos || open - i > 6) fail("Unsupported message control");
                const auto tag = content.substr(i, open - i);
                if (tag != "f" && tag != "f+" && tag != "c" && tag != "cE" && tag != "wE" && tag != "font" && tag != "space" && tag != "-")
                    fail("Unsupported message control: " + tag);
                drawing.append(content, start, close - start + 1); i = close + 1; continue;
            }
            const auto value = static_cast<unsigned char>(content[i]);
            if (value != '\r' && value != '\n' && (value & 0xc0) != 0x80) ++count;
            drawing.push_back(content[i++]);
        }
        message_ = content; set_text(9900025, content); set_number(9000012, 1);
        const int size = number(9000008);
        message_state_ = {{"owner", run.owner}, {"pc", frame.pc - 1}, {"common", frame.context.common},
            {"content", drawing}, {"total", count}, {"visible", 0}, {"progress", 0.0}, {"instant", instant}, {"automatic", automatic},
            {"ready", frame_ + std::max(0, number(9000010))}, {"complete", false}, {"done", false},
            {"x", number(9000001)}, {"y", number(9000002)},
            {"style", {{"size", size}, {"shadow", number(9000021)}, {"speed", 0},
                {"antialias", project_.at("game").value("flags", Json::array()).size() > 14 ? project_.at("game").at("flags").at(14).get<int>() : 0}}}};
    }
    if (message_state_.at("owner") == run.owner && message_state_.at("common") == frame.context.common &&
        message_state_.at("pc") == frame.pc - 1 && message_state_.at("done") == true) {
        message_state_ = Json::object(); message_.clear(); return;
    }
    --frame.pc; run.wake = frame_ + 1;
}

void Runtime::advance_message() {
    if (message_state_.empty() || message_state_.at("done") == true || frame_ < message_state_.at("ready").get<std::uint64_t>()) return;
    const bool confirm = (input_.confirm && !previous_input_.confirm) || input_.mouse_left_new;
    const bool was_complete = message_state_.at("complete");
    const int total = message_state_.at("total"), speed = number(9000009);
    double progress = message_state_.at("progress");
    if (message_state_.at("instant") == true || speed <= 0 || confirm) progress = total;
    else progress += static_cast<double>(speed) / project_.at("game").at("fps").get<int>();
    progress = std::min(progress, static_cast<double>(total));
    message_state_["progress"] = progress; message_state_["visible"] = static_cast<int>(std::floor(progress));
    message_state_["complete"] = progress >= total;
    if ((was_complete && confirm) || (progress >= total && message_state_.at("automatic") == true)) {
        message_state_["done"] = true; set_number(9000012, 0);
    }
}
}
