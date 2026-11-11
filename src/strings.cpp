#include "runtime.hpp"
#include <filesystem>

namespace wolf {
void Runtime::string_operation(const Json& cmd, Context context) {
    const auto& args = cmd.at("args");
    const int options = args.at(1), source = options & 15, operation = (options >> 8) & 15;
    if ((options & ~0x0f1f) || source > 2) fail("Unsupported string-operation flags");
    const int destination = options & 16 ? number(args.at(0), context) : args.at(0).get<int>();
    std::string right;
    if (source == 0) right = expand(cmd.at("strings").at(0), context);
    else right = text(source == 1 ? args.at(2).get<int>() : number(args.at(2), context), context);
    std::string result;
    switch (operation) {
    case 0: result = right; break;
    case 1: result = text(destination, context) + right; break;
    case 5: {
        if (source == 0 && (args.size() != 4 || args.at(2) != 0 || args.at(3) != 1))
            fail("Unsupported text-file encoding/options");
        const std::string exists_prefix = "<<GET_FILE_EXIST>>";
        if (right.compare(0, exists_prefix.size(), exists_prefix) == 0) {
            const auto name = right.substr(exists_prefix.size());
            if (name.find_first_of("*?") != std::string::npos) fail("Wildcard file-existence checks are not implemented");
            result = !name.empty() && name.back() != '/' && name.back() != '\\' && resources_->exists(name, true) ? "1" : "0";
        } else if (right.compare(0, 2, "<<") == 0) fail("Unsupported special string request: " + right.substr(0, right.find(">>") + 2));
        else result = resources_->read_text(right);
        break;
    }
    case 8: case 9: {
        result = text(destination, context);
        const std::string replacement = operation == 8 ? "" : expand(cmd.at("strings").at(1), context);
        if (operation == 9 && source != 0) fail("Variable-source string replacement is not implemented");
        if (!right.empty()) {
            std::size_t offset = 0;
            while ((offset = result.find(right, offset)) != std::string::npos) {
                result.replace(offset, right.size(), replacement); offset += replacement.size();
            }
        }
        break;
    }
    default: fail("Unsupported string operation " + std::to_string(operation));
    }
    set_text(destination, std::move(result), context);
}
}
