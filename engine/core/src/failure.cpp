module nx.core;

import std;

namespace nx::detail {

void appendFailureLine(std::string& out, std::string_view domain, std::string_view code, std::int64_t value,
                       std::string_view context, const std::source_location& where) {
    auto output = std::back_inserter(out);
    if (code.empty()) {
        std::format_to(output, "{}::#{}", domain, value);
    } else {
        std::format_to(output, "{}::{}", domain, code);
    }
    if (!context.empty()) {
        std::format_to(output, ": {}", context);
    }
    std::format_to(output, " [{}:{}]", sourceFileName(where), where.line());
}

} // namespace nx::detail
