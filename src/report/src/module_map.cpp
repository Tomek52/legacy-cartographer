#include "facts/fact_export.hpp"
#include "report/report_writer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lcart::report {

namespace {

using facts::FileFact;
using facts::FileId;
using facts::FunctionFact;
using facts::ModuleFact;
using facts::ModuleId;

constexpr std::string_view replacement_character = "\xEF\xBF\xBD";

/// Names and paths come from analysed code and lcart.toml (untrusted):
/// `text` with each byte that is not part of valid UTF-8, and each control
/// character, replaced by U+FFFD, so no name can end a line (ADR-0017).
std::string printable(std::string_view text) {
    constexpr unsigned char first_printable = 0x20;
    constexpr unsigned char delete_character = 0x7F;
    std::string out;
    out.reserve(text.size());
    while (!text.empty()) {
        const std::size_t length = facts::utf8_sequence_length(text);
        const auto lead = static_cast<unsigned char>(text.front());
        if (length == 0 || (length == 1 && (lead < first_printable || lead == delete_character))) {
            out += replacement_character;
            text.remove_prefix(1);
        } else {
            out += text.substr(0, length);
            text.remove_prefix(length);
        }
    }
    return out;
}

/// `text` as a Markdown code span (ADR-0017): nothing inside a code span is
/// Markdown or HTML, and a list item has no `|` cell separator to break. The
/// fence is one backtick longer than the longest run of backticks in the
/// text; a space pads a text that starts or ends with a backtick, which
/// CommonMark and Python-Markdown both strip again.
std::string code_span(std::string_view text) {
    const std::string content = printable(text);
    std::size_t longest = 0;
    std::size_t run = 0;
    for (const char character : content) {
        run = character == '`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    const std::string fence(longest + 1, '`');
    const bool pad = content.empty() || content.front() == '`' || content.back() == '`';
    const std::string_view space = pad ? " " : "";
    return fence + std::string{space} + content + std::string{space} + fence;
}

/// The code point of the valid UTF-8 sequence of `length` bytes at the
/// start of `text`.
std::uint32_t code_point(std::string_view text, std::size_t length) {
    constexpr std::uint32_t continuation_bits = 0x3F;
    constexpr std::array<std::uint32_t, 5> lead_bits{0, 0x7F, 0x1F, 0x0F, 0x07};
    std::uint32_t value = static_cast<unsigned char>(text[0]) & lead_bits.at(length);
    for (std::size_t i = 1; i < length; ++i) {
        value = (value << 6U) | (static_cast<unsigned char>(text[i]) & continuation_bits);
    }
    return value;
}

bool plain_in_mermaid(std::uint32_t point) {
    const auto in = [point](char first, char last) {
        return point >= static_cast<std::uint32_t>(first) &&
               point <= static_cast<std::uint32_t>(last);
    };
    return in('a', 'z') || in('A', 'Z') || in('0', '9') || point == ' ' || point == '_' ||
           point == '.' || point == '/' || point == '-';
}

/// `text` for a quoted Mermaid label (ADR-0017): letters, digits, space and
/// `_ . / -` stay; every other character becomes Mermaid's entity code
/// `#<decimal code point>;`. So no name can end the label (`"`), start an
/// entity (`#`), become HTML (`<`, `&`), a Markdown string (`` ` ``) or a
/// new statement (`;`, a line break).
std::string mermaid_label(std::string_view text) {
    const std::string content = printable(text);
    std::string_view rest = content;
    std::string out;
    while (!rest.empty()) {
        const std::size_t length = facts::utf8_sequence_length(rest);
        const std::uint32_t point = code_point(rest, length);
        if (plain_in_mermaid(point)) {
            out += rest.front();
        } else {
            out += '#' + std::to_string(point) + ';';
        }
        rest.remove_prefix(length);
    }
    return out;
}

/// "1 file", "2 files".
std::string count_of(std::size_t count, std::string_view singular) {
    std::string text = std::to_string(count) + ' ' + std::string{singular};
    if (count != 1) {
        text += 's';
    }
    return text;
}

struct FileLine {
    std::string path;
    std::size_t definitions{};
};

struct FileGroup {
    std::vector<FileLine> files;

    [[nodiscard]] std::size_t definitions() const {
        std::size_t total = 0;
        for (const FileLine& file : files) {
            total += file.definitions;
        }
        return total;
    }

    [[nodiscard]] std::string summary() const {
        return count_of(files.size(), "file") + ", " +
               count_of(definitions(), "function definition");
    }
};

/// Include and call sites that make one module depend on another.
struct EdgeSites {
    std::size_t includes{};
    std::size_t calls{};

    [[nodiscard]] std::string label() const {
        std::string text;
        if (includes > 0) {
            text = count_of(includes, "include");
        }
        if (calls > 0) {
            text += (text.empty() ? "" : ", ") + count_of(calls, "call");
        }
        return text;
    }
};

/// What the map shows, keyed by names and paths only, so the order is the
/// byte order of names and never that of row ids (NFR-004).
struct ModuleMap {
    facts::AnalysisCoverage coverage;
    std::map<std::string, FileGroup> modules;
    FileGroup unassigned;
    std::map<std::pair<std::string, std::string>, EdgeSites> edges;
};

template <typename T>
common::Status read_into(common::Result<T> result, T& target) {
    if (!result) {
        return std::unexpected(result.error());
    }
    target = std::move(*result);
    return {};
}

common::Result<ModuleMap> read_map(const facts::FactQuery& facts) {
    ModuleMap map;
    std::vector<ModuleFact> modules;
    std::vector<FileFact> files;
    std::vector<FunctionFact> functions;
    for (const common::Status& read :
         {read_into(facts.coverage(), map.coverage), read_into(facts.modules(), modules),
          read_into(facts.files(), files), read_into(facts.functions(), functions)}) {
        if (!read) {
            return std::unexpected(read.error());
        }
    }

    std::map<ModuleId, std::string> module_names;
    for (const ModuleFact& module : modules) {
        module_names.emplace(module.id, module.name);
        map.modules.try_emplace(module.name);
    }
    std::map<FileId, std::size_t> definitions;
    for (const FunctionFact& function : functions) {
        if (function.is_definition) {
            ++definitions[function.file];
        }
    }
    for (const FileFact& file : files) {
        if (file.is_external) {
            continue; // system and other headers outside the root (ADR-0017)
        }
        const auto counted = definitions.find(file.id);
        FileLine line{.path = file.path,
                      .definitions = counted == definitions.end() ? 0 : counted->second};
        const auto module = file.module ? module_names.find(*file.module) : module_names.end();
        FileGroup& group =
            module == module_names.end() ? map.unassigned : map.modules[module->second];
        group.files.push_back(std::move(line));
    }
    for (auto&& [name, group] : map.modules) {
        std::ranges::sort(group.files, {}, &FileLine::path);
    }
    std::ranges::sort(map.unassigned.files, {}, &FileLine::path);

    for (const ModuleFact& module : modules) {
        auto edges = facts.module_dependency_edges(module.id);
        if (!edges) {
            return std::unexpected(edges.error());
        }
        for (const facts::ModuleDependencyFact& edge : *edges) {
            EdgeSites& sites = map.edges[{module_names.at(edge.from), module_names.at(edge.to)}];
            switch (edge.via) {
            case facts::DependencyVia::include:
                ++sites.includes;
                break;
            case facts::DependencyVia::call:
                ++sites.calls;
                break;
            }
        }
    }
    return map;
}

void write_graph(const ModuleMap& map, std::string& doc) {
    doc += "## Module dependencies\n\n";
    if (map.modules.empty()) {
        doc += "No modules.\n\n";
        return;
    }
    doc += "An arrow points from a module to a module it depends on. Its label counts\n"
           "the include directives and direct calls that make the dependency.\n\n"
           "```mermaid\nflowchart LR\n";
    // Generated ids, so no module name has to be a valid Mermaid id.
    std::map<std::string, std::string> ids;
    for (auto&& [name, group] : map.modules) {
        std::string id = "m" + std::to_string(ids.size());
        doc += "    " + id + "[\"" + mermaid_label(name) + "\"]\n";
        ids.emplace(name, std::move(id));
    }
    for (auto&& [modules, sites] : map.edges) {
        doc += "    " + ids.at(modules.first) + " -->|\"" + sites.label() + "\"| " +
               ids.at(modules.second) + '\n';
    }
    doc += "```\n\n";
}

void write_files(const FileGroup& group, std::string& doc) {
    if (group.files.empty()) {
        doc += "No files.\n";
        return;
    }
    for (const FileLine& file : group.files) {
        doc += "- " + code_span(file.path) + ": " +
               count_of(file.definitions, "function definition") + '\n';
    }
}

void write_modules(const ModuleMap& map, std::string& doc) {
    doc += "## Modules\n\n";
    const bool any_unassigned = !map.unassigned.files.empty();
    if (map.modules.empty() && !any_unassigned) {
        doc += "No modules.\n";
        return;
    }
    for (auto&& [name, group] : map.modules) {
        doc += "- " + code_span(name) + ": " + group.summary() + '\n';
    }
    // Plain text, never a code span, so no module name can collide with it.
    if (any_unassigned) {
        doc += "- (unassigned): " + map.unassigned.summary() + '\n';
    }
    for (auto&& [name, group] : map.modules) {
        doc += "\n### Module " + code_span(name) + "\n\n";
        write_files(group, doc);
    }
    if (any_unassigned) {
        doc += "\n### (unassigned)\n\n";
        write_files(map.unassigned, doc);
    }
}

} // namespace

common::Status write_module_map(const facts::FactQuery& facts, std::ostream& out) {
    const auto map = read_map(facts);
    if (!map) {
        return std::unexpected(map.error());
    }
    std::string doc = "# Module map\n\n"
                      "Generated by `lcart map` from the fact database, without an LLM.\n\n";
    doc += "Translation units parsed: " + std::to_string(map->coverage.translation_units_parsed) +
           " of " + std::to_string(map->coverage.translation_units_total) + ".\n\n";
    write_graph(*map, doc);
    write_modules(*map, doc);

    out << doc;
    out.flush();
    if (!out) {
        return std::unexpected(common::Error{.code = common::ErrorCode::io_error,
                                             .message = "cannot write the module map"});
    }
    return {};
}

} // namespace lcart::report
