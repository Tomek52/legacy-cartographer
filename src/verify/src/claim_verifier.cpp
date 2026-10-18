#include "verify/claim_verifier.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// The rules are those of docs/specs/claim-schema.md (entity resolution v2,
// relation rules v2, negation v2); each part below names its step.

namespace lcart::verify {

namespace {

using claims::Claim;
using claims::ClaimKind;
using claims::EntityRef;
using claims::EntityType;
using common::Error;
using common::ErrorCode;
using common::Result;
using facts::CallFact;
using facts::CallKind;
using facts::DiagnosticFact;
using facts::DiagnosticSeverity;
using facts::FactQuery;
using facts::FileFact;
using facts::FunctionFact;
using facts::IncludeFact;
using facts::ModuleDependencyFact;
using facts::ModuleFact;

FactRef ref(facts::ModuleId id) {
    return {.table = FactTable::module, .id = id.value};
}
FactRef ref(facts::FileId id) {
    return {.table = FactTable::file, .id = id.value};
}
FactRef ref(facts::FunctionId id) {
    return {.table = FactTable::function, .id = id.value};
}
FactRef ref(facts::IncludeId id) {
    return {.table = FactTable::include_edge, .id = id.value};
}
FactRef ref(facts::CallId id) {
    return {.table = FactTable::call_edge, .id = id.value};
}

FactRef ref(const ModuleDependencyFact& edge) {
    switch (edge.via) {
    case facts::DependencyVia::include:
        return ref(facts::IncludeId{edge.edge_id});
    case facts::DependencyVia::call:
        return ref(facts::CallId{edge.edge_id});
    }
    std::unreachable();
}

using Entity = std::variant<FileFact, FunctionFact, ModuleFact>;

FactRef ref(const Entity& entity) {
    return std::visit([](const auto& fact) { return ref(fact.id); }, entity);
}

bool is_error(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::warning:
        return false;
    case DiagnosticSeverity::error:
    case DiagnosticSeverity::fatal:
        return true;
    }
    std::unreachable();
}

/// One claim and the queries it is checked against; both outlive it.
class Context {
public:
    Context(const Claim& claim, const FactQuery& facts) : claim_(&claim), facts_(&facts) {}

    [[nodiscard]] const Claim& claim() const { return *claim_; }
    [[nodiscard]] const FactQuery& facts() const { return *facts_; }

    [[nodiscard]] Verdict verdict(VerificationStatus status, VerdictReason reason,
                                  std::vector<FactRef> evidence = {}) const {
        return {.claim_id = claim_->id,
                .status = status,
                .reason = reason,
                .evidence = std::move(evidence)};
    }

    /// At least one translation unit, and every one parsed: a database
    /// without coverage proves nothing absent.
    [[nodiscard]] Result<bool> coverage_complete() const {
        auto coverage = facts_->coverage();
        if (!coverage) {
            return std::unexpected(coverage.error());
        }
        return coverage->translation_units_total > 0 &&
               coverage->translation_units_parsed == coverage->translation_units_total;
    }

    /// "Was parsed" of the relation rules: under the root, no fatal error
    /// anywhere in the analysis, and no error in the file or in the
    /// translation unit it is the main file of. Clang's error recovery can
    /// drop calls; after a fatal error it goes on parsing without reporting
    /// errors, in any file the unit reaches, so nothing locates the damage.
    [[nodiscard]] Result<bool> was_parsed(const FileFact& file) const {
        if (file.is_external) {
            return false; // its directives and definitions are not recorded
        }
        auto diagnostics = facts_->diagnostics();
        if (!diagnostics) {
            return std::unexpected(diagnostics.error());
        }
        return std::ranges::none_of(*diagnostics, [&](const DiagnosticFact& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::fatal ||
                   (is_error(diagnostic.severity) &&
                    (diagnostic.file == file.path || diagnostic.translation_unit == file.path));
        });
    }

    [[nodiscard]] Result<bool> definition_was_parsed(const FunctionFact& function) const {
        if (!function.is_definition) {
            return false;
        }
        auto file = file_of(function);
        if (!file) {
            return std::unexpected(file.error());
        }
        return was_parsed(*file);
    }

    /// The file a function is located in. Every function row names a file
    /// row, so a missing one is an inconsistent database, not a verdict.
    [[nodiscard]] Result<FileFact> file_of(const FunctionFact& function) const {
        auto file = facts_->file(function.file);
        if (!file) {
            return std::unexpected(file.error());
        }
        if (!*file) {
            return std::unexpected(Error{.code = ErrorCode::not_found,
                                         .message = "function " + function.usr +
                                                    ": its file is not in the fact database"});
        }
        return std::move(**file);
    }

private:
    const Claim* claim_;
    const FactQuery* facts_;
};

// --- Step 1: resolve entities ---------------------------------------------

/// `name` is the end of `path` after a `/`: `modbus.c` matches
/// `src/modbus.c` but not `src/libmodbus.c`.
bool ends_with_path(std::string_view path, std::string_view name) {
    return path.size() > name.size() && path.ends_with(name) &&
           path[path.size() - name.size() - 1] == '/';
}

/// The files `name` names: the exact match, else every suffix match, in
/// the order of FactQuery::files().
Result<std::vector<FileFact>> files_named(const FactQuery& facts, std::string_view name) {
    auto exact = facts.find_file(name);
    if (!exact) {
        return std::unexpected(exact.error());
    }
    if (*exact) {
        return std::vector<FileFact>{std::move(**exact)};
    }
    auto files = facts.files();
    if (!files) {
        return std::unexpected(files.error());
    }
    std::vector<FileFact> matching;
    std::ranges::copy_if(*files, std::back_inserter(matching),
                         [&](const FileFact& file) { return ends_with_path(file.path, name); });
    return matching;
}

/// The functions `entity` can mean. The database locates a function at its
/// definition, else its first declaration, so a `file` that locates none of
/// them (a header declaring it) excludes none: it only disambiguates.
Result<std::vector<FunctionFact>> functions_named(const FactQuery& facts, const EntityRef& entity) {
    auto named = facts.find_functions(entity.name);
    if (!named || !entity.file) {
        return named;
    }
    auto files = files_named(facts, *entity.file);
    if (!files) {
        return std::unexpected(files.error());
    }
    std::vector<FunctionFact> located;
    std::ranges::copy_if(*named, std::back_inserter(located), [&](const FunctionFact& function) {
        return std::ranges::any_of(*files,
                                   [&](const FileFact& file) { return file.id == function.file; });
    });
    if (located.empty()) {
        return named;
    }
    return located;
}

Result<std::vector<Entity>> candidates(const FactQuery& facts, const EntityRef& entity) {
    std::vector<Entity> found;
    switch (entity.type) {
    case EntityType::file: {
        auto files = files_named(facts, entity.name);
        if (!files) {
            return std::unexpected(files.error());
        }
        std::ranges::move(*files, std::back_inserter(found));
        return found;
    }
    case EntityType::function: {
        auto functions = functions_named(facts, entity);
        if (!functions) {
            return std::unexpected(functions.error());
        }
        std::ranges::move(*functions, std::back_inserter(found));
        return found;
    }
    case EntityType::module: {
        auto module = facts.find_module(entity.name);
        if (!module) {
            return std::unexpected(module.error());
        }
        if (*module) {
            found.emplace_back(std::move(**module));
        }
        return found;
    }
    }
    std::unreachable();
}

/// The entity, or else the verdict, the same for both polarities, when it
/// does not resolve to exactly one.
struct Resolution {
    std::optional<Entity> entity;
    Verdict verdict;
};

Result<Resolution> resolve(const Context& context, const EntityRef& entity) {
    auto found = candidates(context.facts(), entity);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (found->size() == 1) {
        return Resolution{.entity = std::move(found->front()), .verdict = {}};
    }
    if (found->size() > 1) {
        std::vector<FactRef> evidence;
        std::ranges::transform(*found, std::back_inserter(evidence),
                               [](const Entity& candidate) { return ref(candidate); });
        return Resolution{.entity = std::nullopt,
                          .verdict = context.verdict(VerificationStatus::unverified,
                                                     VerdictReason::entity_ambiguous,
                                                     std::move(evidence))};
    }
    auto complete = context.coverage_complete();
    if (!complete) {
        return std::unexpected(complete.error());
    }
    // No fact to cite: the coverage is the evidence (verdict.hpp).
    return Resolution{.entity = std::nullopt,
                      .verdict = *complete ? context.verdict(VerificationStatus::contradicted,
                                                             VerdictReason::entity_not_found)
                                           : context.verdict(VerificationStatus::unverified,
                                                             VerdictReason::coverage_incomplete)};
}

// --- Step 2: check the relation (affirmed) --------------------------------

Verdict found(const Context& context, std::vector<FactRef> evidence) {
    return context.verdict(VerificationStatus::confirmed, VerdictReason::relation_found,
                           std::move(evidence));
}

Verdict absent(const Context& context, std::vector<FactRef> evidence) {
    return context.verdict(VerificationStatus::contradicted, VerdictReason::relation_absent,
                           std::move(evidence));
}

Verdict cannot_tell(const Context& context) {
    return context.verdict(VerificationStatus::unverified, VerdictReason::coverage_incomplete);
}

Result<Verdict> check_calls(const Context& context, const FunctionFact& subject,
                            const FunctionFact& object) {
    auto calls = context.facts().calls_from(subject.id);
    if (!calls) {
        return std::unexpected(calls.error());
    }
    std::vector<FactRef> direct;
    std::vector<FactRef> indirect;
    for (const CallFact& call : *calls) {
        if (call.kind == CallKind::direct && call.callee == object.id) {
            direct.push_back(ref(call.id));
        } else if (call.kind == CallKind::indirect) {
            indirect.push_back(ref(call.id));
        }
    }
    if (!direct.empty()) {
        return found(context, std::move(direct));
    }
    auto parsed = context.definition_was_parsed(subject);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (!*parsed) {
        return cannot_tell(context);
    }
    // A may_call edge is never a call (FR-015): the object may be reached
    // through a pointer, so the claim cannot be decided either way.
    auto may_calls = context.facts().may_calls_from(subject.id);
    if (!may_calls) {
        return std::unexpected(may_calls.error());
    }
    const bool has_indirect_calls = !indirect.empty() || !may_calls->empty();
    for (const CallFact& may_call : *may_calls) {
        if (may_call.callee == object.id) {
            indirect.push_back(ref(may_call.id));
        }
    }
    if (has_indirect_calls) {
        return context.verdict(VerificationStatus::unverified,
                               VerdictReason::indirect_calls_present, std::move(indirect));
    }
    return absent(context, {ref(subject.id)});
}

Result<Verdict> check_includes(const Context& context, const FileFact& subject,
                               const FileFact& object) {
    auto includes = context.facts().includes_of(subject.id);
    if (!includes) {
        return std::unexpected(includes.error());
    }
    std::vector<FactRef> directives;
    for (const IncludeFact& include : *includes) {
        if (include.included == object.id) {
            directives.push_back(ref(include.id));
        }
    }
    if (!directives.empty()) {
        return found(context, std::move(directives));
    }
    auto parsed = context.was_parsed(subject);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return *parsed ? absent(context, {ref(subject.id)}) : cannot_tell(context);
}

/// The may_call edges from a function of `from` to a function of `to`, in
/// the order of FactQuery::may_calls().
Result<std::vector<FactRef>> may_calls_between(const Context& context, const ModuleFact& from,
                                               const ModuleFact& to) {
    auto may_calls = context.facts().may_calls();
    if (!may_calls) {
        return std::unexpected(may_calls.error());
    }
    auto module_of = [&](facts::FunctionId id) -> Result<std::optional<facts::ModuleId>> {
        auto function = context.facts().function(id);
        if (!function) {
            return std::unexpected(function.error());
        }
        if (!*function) {
            return std::unexpected(Error{.code = ErrorCode::not_found,
                                         .message = "a may_call edge names a function that is "
                                                    "not in the fact database"});
        }
        auto file = context.file_of(**function);
        if (!file) {
            return std::unexpected(file.error());
        }
        return file->module;
    };
    std::vector<FactRef> between;
    for (const CallFact& may_call : *may_calls) {
        auto caller_module = module_of(may_call.caller);
        if (!caller_module) {
            return std::unexpected(caller_module.error());
        }
        if (*caller_module != from.id || !may_call.callee) {
            continue;
        }
        auto callee_module = module_of(*may_call.callee);
        if (!callee_module) {
            return std::unexpected(callee_module.error());
        }
        if (*callee_module == to.id) {
            between.push_back(ref(may_call.id));
        }
    }
    return between;
}

Result<Verdict> check_depends_on(const Context& context, const ModuleFact& subject,
                                 const ModuleFact& object) {
    auto edges = context.facts().module_dependency_edges(subject.id);
    if (!edges) {
        return std::unexpected(edges.error());
    }
    std::vector<FactRef> justifying;
    for (const ModuleDependencyFact& edge : *edges) {
        if (edge.to == object.id) {
            justifying.push_back(ref(edge));
        }
    }
    if (!justifying.empty()) {
        return found(context, std::move(justifying));
    }
    // may_call edges are no module dependencies (ADR-0026), but a call
    // through a table may still reach the object module.
    auto may_calls = may_calls_between(context, subject, object);
    if (!may_calls) {
        return std::unexpected(may_calls.error());
    }
    if (!may_calls->empty()) {
        return context.verdict(VerificationStatus::unverified,
                               VerdictReason::indirect_calls_present, std::move(*may_calls));
    }
    auto complete = context.coverage_complete();
    if (!complete) {
        return std::unexpected(complete.error());
    }
    return *complete ? absent(context, {ref(subject.id)}) : cannot_tell(context);
}

Result<Verdict> check_defines(const Context& context, const FileFact& subject,
                              const FunctionFact& object) {
    if (object.is_definition && object.file == subject.id) {
        return found(context, {ref(object.id)});
    }
    // Error recovery can drop a definition, so absence counts only in a
    // file that was parsed.
    auto parsed = context.was_parsed(subject);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return *parsed ? absent(context, {ref(subject.id)}) : cannot_tell(context);
}

Result<Verdict> check_belongs_to(const Context& context, const Entity& subject,
                                 const ModuleFact& object) {
    std::optional<FileFact> file;
    std::vector<FactRef> evidence;
    if (const auto* function = std::get_if<FunctionFact>(&subject)) {
        // A function belongs to the module of its file.
        auto function_file = context.file_of(*function);
        if (!function_file) {
            return std::unexpected(function_file.error());
        }
        file = std::move(*function_file);
        evidence.push_back(ref(function->id));
    } else {
        file = std::get<FileFact>(subject);
    }
    evidence.push_back(ref(file->id));
    const bool belongs = file->module == object.id;
    return belongs ? found(context, std::move(evidence)) : absent(context, std::move(evidence));
}

/// The parser checks these after schema validation, but claims can also be
/// built in code.
bool types_fit(const Claim& claim) {
    const EntityType subject = claim.subject.type;
    const EntityType object = claim.object.type;
    switch (claim.kind) {
    case ClaimKind::calls:
        return subject == EntityType::function && object == EntityType::function;
    case ClaimKind::includes:
        return subject == EntityType::file && object == EntityType::file;
    case ClaimKind::depends_on:
        return subject == EntityType::module && object == EntityType::module;
    case ClaimKind::defines:
        return subject == EntityType::file && object == EntityType::function;
    case ClaimKind::belongs_to:
        return (subject == EntityType::file || subject == EntityType::function) &&
               object == EntityType::module;
    }
    std::unreachable();
}

Result<Verdict> check_relation(const Context& context, const Entity& subject,
                               const Entity& object) {
    switch (context.claim().kind) {
    case ClaimKind::calls:
        return check_calls(context, std::get<FunctionFact>(subject),
                           std::get<FunctionFact>(object));
    case ClaimKind::includes:
        return check_includes(context, std::get<FileFact>(subject), std::get<FileFact>(object));
    case ClaimKind::depends_on:
        return check_depends_on(context, std::get<ModuleFact>(subject),
                                std::get<ModuleFact>(object));
    case ClaimKind::defines:
        return check_defines(context, std::get<FileFact>(subject), std::get<FunctionFact>(object));
    case ClaimKind::belongs_to:
        return check_belongs_to(context, subject, std::get<ModuleFact>(object));
    }
    std::unreachable();
}

// --- Step 3: negation -----------------------------------------------------

Verdict negated(Verdict verdict) {
    switch (verdict.status) {
    case VerificationStatus::confirmed:
        verdict.status = VerificationStatus::contradicted;
        return verdict;
    case VerificationStatus::contradicted:
        verdict.status = VerificationStatus::confirmed;
        return verdict;
    case VerificationStatus::unverified:
        // Also a negated `calls` with indirect calls: the call may still
        // happen through a pointer.
        return verdict;
    }
    std::unreachable();
}

} // namespace

Result<Verdict> verify_claim(const Claim& claim, const FactQuery& facts) {
    if (!types_fit(claim)) {
        return std::unexpected(
            Error{.code = ErrorCode::invalid_argument,
                  .message = "claim " + claim.id + ": its entity types do not fit its kind"});
    }
    const Context context{claim, facts};
    auto subject = resolve(context, claim.subject);
    if (!subject) {
        return std::unexpected(subject.error());
    }
    if (!subject->entity) {
        return std::move(subject->verdict);
    }
    auto object = resolve(context, claim.object);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (!object->entity) {
        return std::move(object->verdict);
    }
    auto relation = check_relation(context, *subject->entity, *object->entity);
    if (!relation || claim.polarity == claims::Polarity::affirmed) {
        return relation;
    }
    return negated(std::move(*relation));
}

} // namespace lcart::verify
