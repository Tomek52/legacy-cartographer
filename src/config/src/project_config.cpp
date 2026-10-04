#include "config/project_config.hpp"

// toml++ 3.4.0 (ADR-0015) checks some preconditions with TOML_ASSERT_ASSUME, which is
// assert() in debug builds and __builtin_assume() under NDEBUG, and at least
// one is reachable from malformed input (a table header without a key,
// `[=]`, found by fuzz/project_config_fuzzer.cpp): an abort, or undefined
// behaviour in a release build. Its error path for that input exists, so
// the assertions are made no-ops and NDEBUG hidden while the header is
// read. The header is included in this file only, so no other translation
// unit sees toml++'s inline functions compiled differently. The nesting
// limit is the one toml++ lowered after 3.4.0 against stack overflow in
// sanitizer builds (ADR-0015).
#pragma push_macro("NDEBUG")
#undef NDEBUG
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): toml++ is configured only by macros.
#define TOML_ASSERT(expr) static_cast<void>(0)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): toml++ is configured only by macros.
#define TOML_MAX_NESTED_VALUES 128
#include <toml++/toml.hpp>
#pragma pop_macro("NDEBUG")

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lcart::config {

namespace {

using common::Error;
using common::ErrorCode;
using common::Result;
using common::Status;

/// Switches that give consent to send or include source code: command line
/// only, so a checked-in file cannot give them (ADR-0008).
constexpr std::array consent_switches{std::string_view{"include_source"},
                                      std::string_view{"allow_remote_source"}};

[[nodiscard]] bool is_consent_switch(std::string_view key) {
    std::string normalised{key};
    std::ranges::replace(normalised, '-', '_');
    return std::ranges::find(consent_switches, normalised) != consent_switches.end();
}

constexpr std::string_view utf8_byte_order_mark = "\xEF\xBB\xBF";

/// The position of the first non-ASCII byte of `text`, if any. toml++ 3.4.0
/// reaches __builtin_unreachable() in its Unicode whitespace tables for some
/// code points (U+030C, found by fuzz/project_config_fuzzer.cpp; fixed
/// upstream after 3.4.0), so non-ASCII text never reaches it (ADR-0015).
[[nodiscard]] std::optional<toml::source_position> first_non_ascii(std::string_view text) {
    toml::source_position position{.line = 1, .column = 1};
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x80U) {
            return position;
        }
        if (c == '\n') {
            ++position.line;
            position.column = 1;
        } else {
            ++position.column;
        }
    }
    return std::nullopt;
}

/// Limits that keep toml++ 3.4.0 within its stack: it recurses once per
/// dotted key part and table level, without a limit (ADR-0015).
constexpr std::size_t max_dotted_parts_per_line = 32;
constexpr std::size_t max_size = std::size_t{1024} * 1024;

/// The position of the first `.` beyond max_dotted_parts_per_line outside
/// strings and comments on one line, in ASCII `text`. Dots in numbers count
/// too: a bare key may be all digits (`0.0.0 = 1`). A newline inside a
/// string does not reset the count, which only errs on the safe side.
[[nodiscard]] std::optional<toml::source_position> too_many_dots(std::string_view text) {
    enum class In : std::uint8_t { code, comment, basic, literal, multi_basic, multi_literal };
    In in = In::code;
    std::size_t dots = 0;
    toml::source_position position{.line = 1, .column = 1};
    std::size_t at = 0;
    auto advance = [&](std::size_t count) {
        for (; count > 0 && at < text.size(); --count, ++at) {
            if (text[at] == '\n') {
                ++position.line;
                position.column = 1;
            } else {
                ++position.column;
            }
        }
    };
    auto starts = [&](std::string_view token) {
        return text.substr(at).starts_with(token);
    };
    while (at < text.size()) {
        const char c = text[at];
        switch (in) {
        case In::code:
            if (c == '#') {
                in = In::comment;
            } else if (starts(R"(""")")) {
                in = In::multi_basic;
                advance(2);
            } else if (starts("'''")) {
                in = In::multi_literal;
                advance(2);
            } else if (c == '"') {
                in = In::basic;
            } else if (c == '\'') {
                in = In::literal;
            } else if (c == '\n') {
                dots = 0;
            } else if (c == '.' && ++dots > max_dotted_parts_per_line) {
                return position;
            }
            break;
        case In::comment:
        case In::literal:
            if (c == '\n' || (in == In::literal && c == '\'')) {
                in = In::code;
                dots = c == '\n' ? 0 : dots;
            }
            break;
        case In::basic:
        case In::multi_basic:
            if (c == '\\') {
                advance(1); // the escaped character
            } else if (in == In::basic && (c == '"' || c == '\n')) {
                in = In::code;
            } else if (in == In::multi_basic && starts(R"(""")")) {
                in = In::code;
                advance(2);
            }
            break;
        case In::multi_literal:
            if (starts("'''")) {
                in = In::code;
                advance(2);
            }
            break;
        }
        advance(1);
    }
    return std::nullopt;
}

/// Builds errors located in one configuration file.
class Reporter {
public:
    explicit Reporter(std::string_view source_name) : source_name_(source_name) {}

    [[nodiscard]] Error whole_file(std::string_view message) const {
        return Error{.code = ErrorCode::parse_error,
                     .message = source_name_ + ": " + std::string{message}};
    }

    [[nodiscard]] Error at(const toml::source_region& region, std::string_view message,
                           ErrorCode code = ErrorCode::invalid_argument) const {
        return Error{.code = code,
                     .message = source_name_ + ':' + std::to_string(region.begin.line) + ':' +
                                std::to_string(region.begin.column) + ": " + std::string{message}};
    }

private:
    std::string source_name_;
};

/// Finds a consent switch at any depth, before any other check, so that it
/// is reported as such even inside an unknown table. Breadth first, without
/// recursion: the depth is the input's.
[[nodiscard]] Status reject_consent_switches(const Reporter& report, const toml::table& document) {
    std::vector<const toml::node*> pending{&document};
    for (std::size_t next = 0; next < pending.size(); ++next) {
        if (const toml::table* table = pending[next]->as_table()) {
            for (auto&& [key, value] : *table) {
                if (is_consent_switch(key.str())) {
                    return std::unexpected(
                        report.at(key.source(), "'" + std::string{key.str()} +
                                                    "' is given on the command line only, never "
                                                    "in the configuration file (ADR-0008)"));
                }
                pending.push_back(&value);
            }
        } else if (const toml::array* array = pending[next]->as_array()) {
            for (const toml::node& element : *array) {
                pending.push_back(&element);
            }
        }
    }
    return {};
}

[[nodiscard]] Error unknown_key(const Reporter& report, const toml::key& key,
                                std::string_view prefix) {
    return report.at(key.source(),
                     "unknown key '" + std::string{prefix} + std::string{key.str()} + "'");
}

[[nodiscard]] Result<std::string> string_of(const Reporter& report, const toml::node& node,
                                            std::string_view name) {
    const toml::value<std::string>* text = node.as_string();
    if (text == nullptr) {
        return std::unexpected(
            report.at(node.source(), "'" + std::string{name} + "' must be a string"));
    }
    return text->get();
}

/// An array of path globs, such as `modules.paths`, called `name` in errors.
[[nodiscard]] Result<std::vector<PathGlob>>
read_globs(const Reporter& report, const toml::node& node, std::string_view name) {
    const std::string must = "'" + std::string{name} + "' must be an array of strings";
    const toml::array* paths = node.as_array();
    if (paths == nullptr) {
        return std::unexpected(report.at(node.source(), must));
    }
    std::vector<PathGlob> globs;
    for (const toml::node& element : *paths) {
        const toml::value<std::string>* text = element.as_string();
        if (text == nullptr) {
            return std::unexpected(report.at(element.source(), must));
        }
        auto glob = PathGlob::parse(text->get());
        if (!glob) {
            return std::unexpected(report.at(element.source(), glob.error().message));
        }
        globs.push_back(std::move(*glob));
    }
    return globs;
}

[[nodiscard]] Status read_project(const Reporter& report, const toml::node& node,
                                  ProjectConfig& config) {
    const toml::table* project = node.as_table();
    if (project == nullptr) {
        return std::unexpected(report.at(node.source(), "'project' must be a table"));
    }
    for (auto&& [key, value] : *project) {
        if (key == "exclude") {
            auto globs = read_globs(report, value, "project.exclude");
            if (!globs) {
                return std::unexpected(globs.error());
            }
            config.exclude = std::move(*globs);
            continue;
        }
        std::string* field = nullptr;
        if (key == "name") {
            field = &config.name;
        } else if (key == "root") {
            field = &config.root;
        } else if (key == "compilation_database") {
            field = &config.compilation_database;
        } else {
            return std::unexpected(unknown_key(report, key, "project."));
        }
        auto text = string_of(report, value, "project." + std::string{key.str()});
        if (!text) {
            return std::unexpected(text.error());
        }
        *field = std::move(*text);
    }
    return {};
}

[[nodiscard]] Result<ModuleRule> read_module(const Reporter& report, const toml::table& module) {
    ModuleRule rule;
    std::optional<toml::source_region> name_at;
    std::optional<toml::source_region> paths_at;
    for (auto&& [key, value] : module) {
        if (key == "name") {
            auto name = string_of(report, value, "modules.name");
            if (!name) {
                return std::unexpected(name.error());
            }
            rule.name = std::move(*name);
            name_at = value.source();
        } else if (key == "paths") {
            auto paths = read_globs(report, value, "modules.paths");
            if (!paths) {
                return std::unexpected(paths.error());
            }
            rule.paths = std::move(*paths);
            paths_at = value.source();
        } else {
            return std::unexpected(unknown_key(report, key, "modules."));
        }
    }
    if (rule.name.empty()) {
        return std::unexpected(
            report.at(name_at.value_or(module.source()), "module without 'name'"));
    }
    if (rule.paths.empty()) {
        return std::unexpected(report.at(paths_at.value_or(module.source()),
                                         "module '" + rule.name + "' without 'paths'"));
    }
    return rule;
}

[[nodiscard]] Status read_modules(const Reporter& report, const toml::node& node,
                                  ProjectConfig& config) {
    const toml::array* modules = node.as_array();
    if (modules == nullptr || !modules->is_array_of_tables()) {
        return std::unexpected(
            report.at(node.source(), "'modules' must be an array of tables ([[modules]])"));
    }
    for (const toml::node& element : *modules) {
        auto rule = read_module(report, *element.as_table());
        if (!rule) {
            return std::unexpected(rule.error());
        }
        config.modules.push_back(std::move(*rule));
    }
    return {};
}

} // namespace

Result<ProjectConfig> parse_project_config(std::string_view text, std::string_view source_name) {
    const Reporter report{source_name};
    if (text.starts_with(utf8_byte_order_mark)) {
        text.remove_prefix(utf8_byte_order_mark.size());
    }
    if (text.size() > max_size) {
        return std::unexpected(
            report.whole_file("larger than 1 MiB; lcart.toml is a small file (ADR-0015)"));
    }
    if (const std::optional<toml::source_position> at = first_non_ascii(text)) {
        return std::unexpected(report.at({.begin = *at, .end = *at, .path = {}},
                                         "non-ASCII character; lcart.toml must be ASCII (ADR-0015)",
                                         ErrorCode::parse_error));
    }
    if (const std::optional<toml::source_position> at = too_many_dots(text)) {
        return std::unexpected(report.at({.begin = *at, .end = *at, .path = {}},
                                         "more than 32 dotted parts in the keys of one line "
                                         "(ADR-0015)",
                                         ErrorCode::parse_error));
    }
    toml::parse_result parsed = toml::parse(text, source_name);
    if (!parsed) {
        return std::unexpected(report.at(parsed.error().source(), parsed.error().description(),
                                         ErrorCode::parse_error));
    }
    const toml::table& document = parsed.table();
    if (auto allowed = reject_consent_switches(report, document); !allowed) {
        return std::unexpected(allowed.error());
    }

    ProjectConfig config;
    for (auto&& [key, value] : document) {
        Status read;
        if (key == "project") {
            read = read_project(report, value, config);
        } else if (key == "modules") {
            read = read_modules(report, value, config);
        } else {
            read = std::unexpected(unknown_key(report, key, ""));
        }
        if (!read) {
            return std::unexpected(read.error());
        }
    }
    return config;
}

Result<ProjectConfig> load_project_config(const std::filesystem::path& file) {
    const auto unreadable = [&file] {
        return std::unexpected(Error{.code = ErrorCode::io_error,
                                     .message = "cannot read configuration file " + file.string()});
    };
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return unreadable();
    }
    std::string text;
    try {
        std::ifstream in{file, std::ios::binary};
        text.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
        if (!in.is_open() || in.bad()) {
            return unreadable();
        }
    } catch (const std::ios_base::failure&) {
        // libstdc++'s filebuf throws on a failed read() even without
        // exceptions() set; it does not cross this function.
        return unreadable();
    }
    return parse_project_config(text, file.string());
}

} // namespace lcart::config
