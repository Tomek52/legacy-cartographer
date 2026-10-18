#include "handoff/handoff.hpp"

#include "common/sha256.hpp"
#include "common/version.hpp"
#include "facts/fact_export.hpp"
#include "handoff/contract_texts.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace lcart::handoff {

namespace {

namespace fs = std::filesystem;
using common::Error;
using common::ErrorCode;
using common::Result;
using common::Status;
using facts::CallFact;
using facts::FileId;
using facts::FunctionId;
using facts::ModuleId;
using Json = nlohmann::json;

constexpr std::string_view manifest_file = "MANIFEST.json";

/// Every file of a facts-only bundle (contract v1). An existing directory
/// holding nothing else may be replaced (ADR-0028).
constexpr std::array<std::string_view, 9> bundle_file_names{
    "CONTRACT.md",         "MANIFEST.json",      "PROMPT.md",
    "claim-schema.json",   "facts/calls.json",   "facts/functions.json",
    "facts/includes.json", "facts/modules.json", "facts/project.json"};
constexpr std::string_view facts_directory = "facts";

constexpr std::string_view outside_root = "<outside-root>/";
constexpr std::string_view inside_root = "<root>/";
constexpr std::string_view replacement_character = "\xEF\xBF\xBD";

/// Run IDs are this many hexadecimal digits of the SHA-256 (ADR-0028).
constexpr std::size_t run_id_length = 12;

Error error(ErrorCode code, std::string message) {
    return Error{.code = code, .message = std::move(message)};
}

/// MANIFEST.json and facts/*.json (ADR-0028): sorted keys (nlohmann_json's
/// objects are sorted maps), two spaces, invalid UTF-8 as U+FFFD.
std::string pretty(const Json& json) {
    constexpr int indentation = 2;
    return json.dump(indentation, ' ', false, Json::error_handler_t::replace) + '\n';
}

std::string compact(const Json& json) {
    return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

bool is_ascii_letter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

/// `/x`, `\x`, `C:/x` or `C:\x`: a spelling that names a place on some
/// machine, which a bundle must not show (ADR-0028).
bool is_absolute_spelling(std::string_view spelled) {
    if (spelled.starts_with('/') || spelled.starts_with('\\')) {
        return true;
    }
    return spelled.size() >= 3 && is_ascii_letter(spelled[0]) && spelled[1] == ':' &&
           (spelled[2] == '/' || spelled[2] == '\\');
}

std::string_view last_component(std::string_view spelled) {
    const std::size_t separator = spelled.find_last_of("/\\");
    return separator == std::string_view::npos ? spelled : spelled.substr(separator + 1);
}

/// A directive's spelling as a bundle counts it when naming files outside
/// the root (ADR-0028).
std::string bundle_spelling(std::string_view spelled) {
    if (is_absolute_spelling(spelled)) {
        return std::string{outside_root} + std::string{last_component(spelled)};
    }
    return std::string{spelled};
}

/// `text` with every control character (C0, DEL, C1) and every byte that
/// is not valid UTF-8 replaced by U+FFFD, so it stays on one line of
/// PROMPT.md (ADR-0028).
std::string without_control_characters(std::string_view text) {
    constexpr unsigned char first_printable = 0x20;
    constexpr unsigned char delete_character = 0x7F;
    constexpr unsigned char c1_lead = 0xC2;
    constexpr unsigned char c1_end = 0xA0;
    std::string result;
    while (!text.empty()) {
        const std::size_t length = facts::utf8_sequence_length(text);
        const auto lead = static_cast<unsigned char>(text[0]);
        const bool control =
            (length == 1 && (lead < first_printable || lead == delete_character)) ||
            (length == 2 && lead == c1_lead && static_cast<unsigned char>(text[1]) < c1_end);
        if (length == 0 || control) {
            result += replacement_character;
            text.remove_prefix(1);
            continue;
        }
        result += text.substr(0, length);
        text.remove_prefix(length);
    }
    return result;
}

/// A sort key part for a number: fixed width, so text order is numeric.
std::string number_key(std::int64_t value) {
    constexpr std::size_t width = 20;
    const std::string text = std::to_string(value);
    return std::string(width - std::min(width, text.size()), '0') + text;
}

/// JSON objects with the key they are sorted by.
class SortedArray {
public:
    void add(std::vector<std::string> key, Json value) {
        items_.emplace_back(std::move(key), std::move(value));
    }
    [[nodiscard]] Json to_json() && {
        std::ranges::sort(
            items_, [](const auto& left, const auto& right) { return left.first < right.first; });
        Json array = Json::array();
        for (auto& item : items_) {
            array.push_back(std::move(item.second));
        }
        return array;
    }

private:
    std::vector<std::pair<std::vector<std::string>, Json>> items_;
};

template <typename T>
Status read_into(Result<T> result, T& target) {
    if (!result) {
        return std::unexpected(result.error());
    }
    target = std::move(*result);
    return {};
}

/// Everything a bundle is built from, with the names it gives files,
/// functions and modules instead of row ids.
struct Snapshot {
    facts::AnalysisCoverage coverage;
    std::vector<facts::ModuleFact> modules;
    std::vector<facts::FileFact> files;
    std::vector<facts::FunctionFact> functions;
    std::vector<facts::IncludeFact> includes;
    std::vector<CallFact> calls;
    std::vector<CallFact> may_calls;
    std::map<ModuleId, std::vector<facts::ModuleDependencyFact>> dependencies;
    std::map<FileId, facts::ExportedFile> file_names;
    std::map<FileId, ModuleId> file_modules;
    std::map<ModuleId, std::string> module_names;
    std::map<FunctionId, const facts::FunctionFact*> functions_by_id;

    [[nodiscard]] const std::string& file_name(FileId id) const { return file_names.at(id).name; }
    [[nodiscard]] const facts::FunctionFact& function(FunctionId id) const {
        return *functions_by_id.at(id);
    }
    [[nodiscard]] std::optional<ModuleId> module_of(FileId file) const {
        const auto found = file_modules.find(file);
        return found == file_modules.end() ? std::nullopt : std::optional{found->second};
    }
};

Result<Snapshot> read_snapshot(const facts::FactQuery& facts) {
    Snapshot snapshot;
    for (const Status& read :
         {read_into(facts.coverage(), snapshot.coverage),
          read_into(facts.modules(), snapshot.modules), read_into(facts.files(), snapshot.files),
          read_into(facts.functions(), snapshot.functions),
          read_into(facts.includes(), snapshot.includes), read_into(facts.calls(), snapshot.calls),
          read_into(facts.may_calls(), snapshot.may_calls)}) {
        if (!read) {
            return std::unexpected(read.error());
        }
    }
    for (const facts::ModuleFact& module : snapshot.modules) {
        auto edges = facts.module_dependency_edges(module.id);
        if (!edges) {
            return std::unexpected(edges.error());
        }
        snapshot.dependencies.emplace(module.id, std::move(*edges));
        snapshot.module_names.emplace(module.id, module.name);
    }
    snapshot.file_names = file_names_in_bundle(snapshot.files, snapshot.includes);
    for (const facts::FileFact& file : snapshot.files) {
        if (file.module) {
            snapshot.file_modules.emplace(file.id, *file.module);
        }
    }
    for (const facts::FunctionFact& function : snapshot.functions) {
        snapshot.functions_by_id.emplace(function.id, &function);
    }
    return snapshot;
}

/// The modules a bundle covers: `requested`, or all if it is empty.
Result<std::set<ModuleId>> selected_modules(const Snapshot& snapshot,
                                            const std::vector<std::string>& requested) {
    std::set<ModuleId> selected;
    if (requested.empty()) {
        for (const facts::ModuleFact& module : snapshot.modules) {
            selected.insert(module.id);
        }
        return selected;
    }
    std::set<std::string> seen;
    for (const std::string& name : requested) {
        if (!seen.insert(name).second) {
            return std::unexpected(
                error(ErrorCode::invalid_argument, "--module " + name + " given twice"));
        }
        const auto module = std::ranges::find(snapshot.modules, name, &facts::ModuleFact::name);
        if (module == snapshot.modules.end()) {
            return std::unexpected(
                error(ErrorCode::not_found, "no module '" + name + "' in the fact database"));
        }
        selected.insert(module->id);
    }
    return selected;
}

Json project_json(const Snapshot& snapshot, const std::string& name) {
    return Json{{"name", name},
                {"coverage",
                 {{"parsed", snapshot.coverage.translation_units_parsed},
                  {"total", snapshot.coverage.translation_units_total}}},
                {"tool_version", std::string{common::version}},
                {"fact_schema_version", facts::schema_version}};
}

std::string dependency_via(facts::DependencyVia via) {
    switch (via) {
    case facts::DependencyVia::include:
        return "include";
    case facts::DependencyVia::call:
        return "call";
    }
    std::unreachable();
}

Json modules_json(const Snapshot& snapshot, const std::set<ModuleId>& selected) {
    Json modules = Json::array();
    for (const facts::ModuleFact& module : snapshot.modules) { // by name
        if (!selected.contains(module.id)) {
            continue;
        }
        std::vector<std::string> files;
        for (const facts::FileFact& file : snapshot.files) {
            if (file.module == module.id) {
                files.push_back(snapshot.file_name(file.id));
            }
        }
        std::ranges::sort(files);
        std::map<std::string, SortedArray> edges_by_module;
        for (const facts::ModuleDependencyFact& edge : snapshot.dependencies.at(module.id)) {
            const bool is_include = edge.via == facts::DependencyVia::include;
            const std::string& file = snapshot.file_name(edge.site.file);
            // An include edge has no column (0 in the store).
            const Json column = is_include ? Json(nullptr) : Json(edge.site.column);
            edges_by_module[snapshot.module_names.at(edge.to)].add(
                {is_include ? "0" : "1", file, number_key(edge.site.line),
                 number_key(edge.site.column)},
                Json{{"via", dependency_via(edge.via)},
                     {"file", file},
                     {"line", edge.site.line},
                     {"column", column}});
        }
        Json depends_on = Json::array();
        for (auto& [to, edges] : edges_by_module) { // by module name
            depends_on.push_back(Json{{"module", to}, {"edges", std::move(edges).to_json()}});
        }
        modules.push_back(Json{{"name", module.name},
                               {"files", std::move(files)},
                               {"depends_on", std::move(depends_on)}});
    }
    return Json{{"modules", std::move(modules)}};
}

const char* linkage_text(facts::Linkage linkage) {
    switch (linkage) {
    case facts::Linkage::internal:
        return "internal";
    case facts::Linkage::external:
        return "external";
    }
    std::unreachable();
}

Json functions_json(const Snapshot& snapshot, const std::set<ModuleId>& selected) {
    std::map<ModuleId, SortedArray> by_module;
    for (const facts::FunctionFact& function : snapshot.functions) {
        const std::optional<ModuleId> module = snapshot.module_of(function.file);
        if (!module || !selected.contains(*module)) {
            continue;
        }
        const std::string& file = snapshot.file_name(function.file);
        by_module[*module].add({file, number_key(function.line_begin), function.usr},
                               Json{{"name", function.name},
                                    {"qualified_name", function.qualified_name},
                                    {"signature", function.signature},
                                    {"file", file},
                                    {"line_begin", function.line_begin},
                                    {"line_end", function.line_end},
                                    {"definition", function.is_definition},
                                    {"linkage", linkage_text(function.linkage)}});
    }
    Json modules = Json::array();
    for (const facts::ModuleFact& module : snapshot.modules) { // by name
        if (selected.contains(module.id)) {
            auto& functions = by_module[module.id];
            modules.push_back(
                Json{{"name", module.name}, {"functions", std::move(functions).to_json()}});
        }
    }
    return Json{{"modules", std::move(modules)}};
}

Json calls_json(const Snapshot& snapshot, const std::set<ModuleId>& selected) {
    SortedArray direct;
    SortedArray indirect;
    SortedArray may_call;
    const auto add = [&snapshot, &selected](SortedArray& list, const CallFact& call,
                                            bool with_callee, bool with_via) {
        const facts::FunctionFact& caller = snapshot.function(call.caller);
        const std::optional<ModuleId> module = snapshot.module_of(caller.file);
        if (!module || !selected.contains(*module)) {
            return;
        }
        const std::string& site = snapshot.file_name(call.site.file);
        Json record{{"caller", caller.name},
                    {"caller_file", snapshot.file_name(caller.file)},
                    {"file", site},
                    {"line", call.site.line},
                    {"column", call.site.column}};
        std::vector<std::string> key{site, number_key(call.site.line), number_key(call.site.column),
                                     caller.name};
        std::string callee_usr;
        if (with_callee && call.callee) {
            const facts::FunctionFact& callee = snapshot.function(*call.callee);
            record["callee"] = callee.name;
            record["callee_file"] = snapshot.file_name(callee.file);
            key.push_back(callee.name);
            callee_usr = callee.usr;
        }
        if (with_via) {
            record["via"] = call.via;
            key.push_back(call.via);
        }
        // Equal names in one place (overloads, statics of the same name)
        // are ordered by identity, never by row id.
        key.push_back(caller.usr);
        key.push_back(callee_usr);
        list.add(std::move(key), std::move(record));
    };
    for (const CallFact& call : snapshot.calls) {
        switch (call.kind) {
        case facts::CallKind::direct:
            add(direct, call, true, false);
            break;
        case facts::CallKind::indirect:
            add(indirect, call, false, true);
            break;
        case facts::CallKind::may_call:
            break; // only from may_calls(), which calls() never returns
        }
    }
    for (const CallFact& call : snapshot.may_calls) {
        if (call.callee) { // the schema gives every may_call edge a callee
            add(may_call, call, true, true);
        }
    }
    return Json{{"direct", std::move(direct).to_json()},
                {"indirect", std::move(indirect).to_json()},
                {"may_call", std::move(may_call).to_json()}};
}

Json includes_json(const Snapshot& snapshot, const std::set<ModuleId>& selected) {
    SortedArray includes;
    for (const facts::IncludeFact& include : snapshot.includes) {
        const std::optional<ModuleId> module = snapshot.module_of(include.includer);
        if (!module || !selected.contains(*module)) {
            continue;
        }
        Json target = nullptr;
        Json target_external = nullptr;
        std::string target_key = "0";
        std::string spelled = bundle_spelling(include.spelled);
        if (include.included) {
            const facts::ExportedFile& exported = snapshot.file_names.at(*include.included);
            target = exported.name;
            target_external = exported.is_external;
            target_key = "1" + exported.name;
            if (!exported.is_external && is_absolute_spelling(include.spelled)) {
                spelled = std::string{inside_root} + exported.name;
            }
        }
        const std::string& file = snapshot.file_name(include.includer);
        includes.add({file, number_key(include.line), spelled, target_key},
                     Json{{"file", file},
                          {"line", include.line},
                          {"spelled", spelled},
                          {"system", include.is_system},
                          {"target", std::move(target)},
                          {"target_external", std::move(target_external)}});
    }
    return Json{{"includes", std::move(includes).to_json()}};
}

/// The prompt of the contract page with its `{{…}}` values filled in.
Result<std::string> filled_prompt(std::string_view prompt,
                                  const std::map<std::string, std::string, std::less<>>& values) {
    std::string result;
    while (!prompt.empty()) {
        const std::size_t open = prompt.find("{{");
        result += prompt.substr(0, open);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t close = prompt.find("}}", open);
        const std::string_view name = prompt.substr(open + 2, close - open - 2);
        const auto value = values.find(name);
        if (close == std::string_view::npos || value == values.end()) {
            return std::unexpected(
                error(ErrorCode::parse_error, "the prompt of docs/specs/handoff-contract.md uses "
                                              "an unknown value {{" +
                                                  std::string{name} + "}}"));
        }
        result += value->second;
        prompt.remove_prefix(close + 2);
    }
    return result;
}

/// Keeps the prompt's facts-only paragraph, without its mark, and drops the
/// with-source one (handoff contract): each is a list item whose
/// continuation lines are indented by two spaces.
std::string facts_only(std::string_view prompt) {
    constexpr std::string_view facts_only_item = "- *facts-only:* ";
    constexpr std::string_view with_source_item = "- *with-source:* ";
    std::string result;
    bool in_with_source = false;
    while (!prompt.empty()) {
        const std::size_t end = prompt.find('\n');
        const std::string_view line =
            prompt.substr(0, end == std::string_view::npos ? prompt.size() : end + 1);
        prompt.remove_prefix(line.size());
        if (line.starts_with(with_source_item)) {
            in_with_source = true;
            continue;
        }
        if (in_with_source && line.starts_with("  ")) {
            continue;
        }
        in_with_source = false;
        if (line.starts_with(facts_only_item)) {
            result += "- ";
            result += line.substr(facts_only_item.size());
        } else {
            result += line;
        }
    }
    return result;
}

/// Fails unless nothing is at `path`, or a directory holding nothing but
/// bundle files (ADR-0028).
Status check_replaceable(const fs::path& path) {
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(path, ec);
    if (!fs::exists(status)) {
        return {};
    }
    const auto refuse = [&path] {
        return std::unexpected(error(ErrorCode::io_error,
                                     path.string() + " exists and is not an lcart handoff "
                                                     "bundle; lcart replaces only its own files"));
    };
    if (!fs::is_directory(status)) {
        return refuse();
    }
    for (auto entry = fs::recursive_directory_iterator{path, ec};
         !ec && entry != fs::recursive_directory_iterator{}; entry.increment(ec)) {
        const std::string relative = entry->path().lexically_relative(path).generic_string();
        const fs::file_status entry_status = entry->symlink_status(ec);
        const bool known_directory = fs::is_directory(entry_status) && relative == facts_directory;
        const bool known_file =
            fs::is_regular_file(entry_status) && std::ranges::contains(bundle_file_names, relative);
        if (ec || (!known_directory && !known_file)) {
            return refuse();
        }
    }
    if (ec) {
        return std::unexpected(
            error(ErrorCode::io_error, "cannot read " + path.string() + ": " + ec.message()));
    }
    return {};
}

Status write_files(const Bundle& bundle, const fs::path& directory) {
    std::error_code ec;
    for (const auto& [relative, content] : bundle.files) {
        const fs::path path = directory / relative;
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected(
                error(ErrorCode::io_error,
                      "cannot create " + path.parent_path().string() + ": " + ec.message()));
        }
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        file << content;
        file.close();
        if (!file) {
            return std::unexpected(error(ErrorCode::io_error, "cannot write " + path.string()));
        }
    }
    return {};
}

} // namespace

std::map<facts::FileId, facts::ExportedFile>
file_names_in_bundle(const std::vector<facts::FileFact>& files,
                     const std::vector<facts::IncludeFact>& includes) {
    return facts::exported_file_names(files, includes, bundle_spelling);
}

std::string project_name(std::string_view analysis_root) {
    const fs::path root{analysis_root};
    std::string name = root.filename().string();
    if (name.empty()) { // a trailing `/`
        name = root.parent_path().filename().string();
    }
    return name.empty() ? std::string{"project"} : name;
}

Result<Bundle> build_bundle(const facts::FactQuery& facts, const BundleOptions& options) {
    if (options.include_source) {
        return std::unexpected(error(ErrorCode::invalid_argument,
                                     "--include-source is not available yet (planned, M2)"));
    }
    const auto snapshot = read_snapshot(facts);
    if (!snapshot) {
        return std::unexpected(snapshot.error());
    }
    const auto selected = selected_modules(*snapshot, options.modules);
    if (!selected) {
        return std::unexpected(selected.error());
    }

    // The run ID (ADR-0028): the facts export, then what MANIFEST.json
    // records about the options.
    std::vector<std::string> module_option = options.modules;
    std::ranges::sort(module_option);
    const Json run_options{{"include_source", false}, {"modules", module_option}};
    std::ostringstream run_input;
    if (auto exported = facts::write_jsonl_export(facts, run_input); !exported) {
        return std::unexpected(exported.error());
    }
    run_input << compact(Json{{"contract_version", contract_version}, {"options", run_options}})
              << '\n';
    Bundle bundle;
    bundle.run_id = common::sha256_hex(run_input.str()).substr(0, run_id_length);

    const std::string name = without_control_characters(options.project_name);
    const auto prompt = filled_prompt(
        contract_texts::prompt_template,
        {{"project.name", name},
         {"coverage.parsed", std::to_string(snapshot->coverage.translation_units_parsed)},
         {"coverage.total", std::to_string(snapshot->coverage.translation_units_total)},
         // Relative to the bundle directory, where the agent starts: no
         // absolute path enters the prompt (ADR-0028).
         {"output_dir", "../../agent/" + bundle.run_id + "/"}});
    if (!prompt) {
        return std::unexpected(prompt.error());
    }

    bundle.files.emplace("PROMPT.md", facts_only(*prompt));
    bundle.files.emplace("CONTRACT.md", contract_texts::agent_output);
    bundle.files.emplace("claim-schema.json", contract_texts::claim_schema);
    bundle.files.emplace("facts/project.json", pretty(project_json(*snapshot, name)));
    bundle.files.emplace("facts/modules.json", pretty(modules_json(*snapshot, *selected)));
    bundle.files.emplace("facts/functions.json", pretty(functions_json(*snapshot, *selected)));
    bundle.files.emplace("facts/calls.json", pretty(calls_json(*snapshot, *selected)));
    bundle.files.emplace("facts/includes.json", pretty(includes_json(*snapshot, *selected)));

    Json files = Json::array();
    for (const auto& [path, content] : bundle.files) { // sorted by path
        files.push_back(Json{{"path", path}, {"sha256", common::sha256_hex(content)}});
    }
    bundle.files.emplace(manifest_file, pretty(Json{{"contract_version", contract_version},
                                                    {"files", std::move(files)},
                                                    {"format", "lcart-handoff"},
                                                    {"options", run_options},
                                                    {"run_id", bundle.run_id},
                                                    {"source_excerpts", Json::array()}}));
    return bundle;
}

fs::path bundle_directory(const fs::path& output_dir, std::string_view run_id) {
    return output_dir / "handoff" / run_id;
}

Result<BundleManifest> write_bundle(const Bundle& bundle, const fs::path& output_dir) {
    const fs::path directory = bundle_directory(output_dir, bundle.run_id);
    const fs::path parent = directory.parent_path();
    // Hidden next to the bundle: a failed run leaves an earlier bundle as
    // it was, and the leftovers of a crashed one are replaced next time.
    const fs::path partial = parent / ("." + bundle.run_id + ".partial");
    const fs::path previous = parent / ("." + bundle.run_id + ".previous");
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) {
        return std::unexpected(
            error(ErrorCode::io_error, "cannot create " + parent.string() + ": " + ec.message()));
    }
    for (const fs::path& path : {directory, partial, previous}) {
        if (auto replaceable = check_replaceable(path); !replaceable) {
            return std::unexpected(replaceable.error());
        }
    }
    fs::remove_all(partial, ec);
    fs::remove_all(previous, ec);
    if (auto written = write_files(bundle, partial); !written) {
        fs::remove_all(partial, ec); // best effort; the run has failed already
        return std::unexpected(written.error());
    }
    const bool replacing = fs::exists(fs::symlink_status(directory, ec));
    ec.clear(); // set when nothing is there
    bool moved_away = false;
    if (replacing) {
        fs::rename(directory, previous, ec);
        moved_away = !ec;
    }
    if (!ec) {
        fs::rename(partial, directory, ec);
    }
    if (ec) {
        const std::string message =
            "cannot move the new bundle to " + directory.string() + ": " + ec.message();
        if (moved_away) {
            // Put the earlier bundle back, or the next run would delete it
            // as the leftover of a crashed run.
            fs::rename(previous, directory, ec);
        }
        fs::remove_all(partial, ec); // best effort; the run has failed already
        return std::unexpected(error(ErrorCode::io_error, message));
    }
    if (replacing) {
        fs::remove_all(previous, ec); // best effort: only an earlier bundle
    }

    BundleManifest manifest{.run_id = bundle.run_id,
                            .directory = directory,
                            .files_written = {},
                            .source_files_included = {}};
    for (const auto& [path, content] : bundle.files) {
        manifest.files_written.emplace_back(path);
    }
    return manifest;
}

} // namespace lcart::handoff
