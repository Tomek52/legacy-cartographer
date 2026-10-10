#include "analysis/argument_filter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace lcart::analysis {

namespace {

// Driver options that load a shared object or configure one, written with
// '=' so they are a single argument (`clang-20 --help-hidden`).
constexpr std::array driver_prefixes{
    std::string_view{"-fplugin="},
    std::string_view{"-fpass-plugin="},
    std::string_view{"--hipspv-pass-plugin="},
    std::string_view{"-fplugin-arg-"},
};

// cc1 options that load or select a plugin and take the next argument as
// their value (`clang-20 -cc1 --help`).
constexpr std::array cc1_options_with_value{
    std::string_view{"-load"},
    std::string_view{"-plugin"},
    std::string_view{"-add-plugin"},
};
constexpr std::string_view cc1_plugin_arg_prefix{"-plugin-arg-"};
constexpr std::string_view cc1_pass_plugin_prefix{"-fpass-plugin="};

// Driver options whose next argument the driver copies into the cc1 command
// unchanged: -Xclang, and -Xpreprocessor (LLVM 20.1.2
// clang/lib/Driver/ToolChains/Clang.cpp line 1236, with -Wp,).
constexpr std::array separate_forwarders{
    std::string_view{"-Xclang"},
    std::string_view{"-Xpreprocessor"},
};
constexpr std::string_view xclang_equals{"-Xclang="};
// -Wp,<a>,<b>: the driver copies each comma-separated value into the cc1
// command (same line of Clang.cpp).
constexpr std::string_view wp_comma{"-Wp,"};

// clang-cl's pass-through option `/clang:<arg>`, which takes the prefixes
// '/' and '-' (clang/Driver/Options.inc, LLVM 20).
constexpr std::array clang_cl_pass_through_prefixes{
    std::string_view{"/clang:"},
    std::string_view{"-clang:"},
};

[[nodiscard]] bool starts_with_any(std::string_view argument, const auto& prefixes) {
    return std::ranges::any_of(
        prefixes, [argument](std::string_view prefix) { return argument.starts_with(prefix); });
}

enum class Cc1Kind : std::uint8_t { harmless, loads_code, loads_code_with_value };

[[nodiscard]] Cc1Kind classify_cc1(std::string_view option) {
    if (std::ranges::find(cc1_options_with_value, option) != cc1_options_with_value.end() ||
        option.starts_with(cc1_plugin_arg_prefix)) {
        return Cc1Kind::loads_code_with_value;
    }
    if (option.starts_with(cc1_pass_plugin_prefix)) {
        return Cc1Kind::loads_code;
    }
    return Cc1Kind::harmless;
}

[[nodiscard]] bool is_separate_forwarder(std::string_view argument) {
    return std::ranges::find(separate_forwarders, argument) != separate_forwarders.end();
}

/// Whether a `-Wp,` argument passes an option that loads code: any of its
/// comma-separated values, classified like a bare cc1 option. The whole
/// argument is removed then, its other values with it.
[[nodiscard]] bool wp_loads_code(std::string_view argument) {
    if (!argument.starts_with(wp_comma)) {
        return false;
    }
    std::string_view values = argument.substr(wp_comma.size());
    while (true) {
        const std::size_t comma = values.find(',');
        if (classify_cc1(values.substr(0, comma)) != Cc1Kind::harmless) {
            return true;
        }
        if (comma == std::string_view::npos) {
            return false;
        }
        values.remove_prefix(comma + 1);
    }
}

/// The cc1 option forwarded by `-Xclang <opt>` or `-Xpreprocessor <opt>`
/// (two arguments) or `-Xclang=<opt>` (one), with the number of arguments
/// it spans.
struct Forwarded {
    std::string_view option;
    std::size_t width;
};

[[nodiscard]] std::optional<Forwarded> forwarded_at(const std::vector<std::string>& args,
                                                    std::size_t i) {
    const std::string_view argument = args[i];
    if (is_separate_forwarder(argument)) {
        if (i + 1 < args.size()) {
            return Forwarded{.option = args[i + 1], .width = 2};
        }
        return Forwarded{.option = {}, .width = 1}; // dangling: nothing forwarded
    }
    if (argument.starts_with(xclang_equals)) {
        return Forwarded{.option = argument.substr(xclang_equals.size()), .width = 1};
    }
    return std::nullopt;
}

/// Number of arguments to remove for a forwarded cc1 option.
[[nodiscard]] std::size_t forwarded_width(const std::vector<std::string>& args, std::size_t i,
                                          const Forwarded& forwarded) {
    if (forwarded.option.empty()) {
        return forwarded.width; // a trailing -Xclang would forward whatever is appended next
    }
    switch (classify_cc1(forwarded.option)) {
    case Cc1Kind::harmless:
        return 0;
    case Cc1Kind::loads_code:
        return forwarded.width;
    case Cc1Kind::loads_code_with_value: {
        // The value is forwarded the same way: -Xclang <v>, -Xclang=<v> or
        // -Xpreprocessor <v>.
        const std::size_t next = i + forwarded.width;
        const auto value = next < args.size() ? forwarded_at(args, next) : std::nullopt;
        return forwarded.width + (value ? value->width : 0);
    }
    }
    std::unreachable();
}

/// Number of arguments starting at `i` that must be removed; 0 keeps it.
[[nodiscard]] std::size_t removal_width(const std::vector<std::string>& args, std::size_t i) {
    const std::string_view argument = args[i];
    if (argument.starts_with('@') || starts_with_any(argument, driver_prefixes) ||
        wp_loads_code(argument)) {
        return 1;
    }
    if (const auto forwarded = forwarded_at(args, i)) {
        return forwarded_width(args, i, *forwarded);
    }
    switch (classify_cc1(argument)) { // bare cc1 form, as in a `-cc1` command
    case Cc1Kind::harmless:
        return 0;
    case Cc1Kind::loads_code:
        return 1;
    case Cc1Kind::loads_code_with_value:
        return 2;
    }
    std::unreachable();
}

using Groups = std::vector<std::vector<std::size_t>>;

/// Adds to each group (ascending indices into `args`) the -Xclang or
/// -Xpreprocessor right before it, unless that is removed already: its value
/// is being removed, and left alone it would forward the next argument.
void add_forwarders(Groups& groups, const std::vector<std::string>& args) {
    std::vector<bool> removed(args.size(), false);
    for (const std::vector<std::size_t>& group : groups) {
        for (const std::size_t index : group) {
            removed[index] = true;
        }
    }
    for (std::vector<std::size_t>& group : groups) {
        const std::size_t first = group.front();
        if (first > 0 && !removed[first - 1] && is_separate_forwarder(args[first - 1])) {
            group.insert(group.begin(), first - 1);
            removed[first - 1] = true;
        }
    }
}

/// Groups of indices into `args` to remove, one per removed option, when
/// the driver reads `args` as one argument list.
[[nodiscard]] Groups removals(const std::vector<std::string>& args) {
    Groups groups;
    std::size_t i = 0;
    while (i < args.size()) {
        const std::size_t width = removal_width(args, i);
        if (width == 0) {
            ++i;
            continue;
        }
        std::vector<std::size_t>& group = groups.emplace_back();
        for (std::size_t k = i; k < i + width && k < args.size(); ++k) {
            group.push_back(k);
        }
        i += width;
    }
    add_forwarders(groups, args);
    return groups;
}

/// The removals in clang-cl's pass-through list: in CL mode the driver
/// parses the values of all `/clang:` and `-clang:` arguments, in order, as
/// a separate list of `clang` driver arguments (LLVM 20.1.2
/// clang/lib/Driver/Driver.cpp, "In CL mode, look for any pass-through
/// arguments"; ADR-0021). Checked in any mode: outside CL mode such an
/// argument is an input file name or an error, so removing it loses nothing.
[[nodiscard]] Groups pass_through_removals(const std::vector<std::string>& args) {
    std::vector<std::string> values;
    std::vector<std::size_t> positions;
    // Also collects a `/clang:` argument that is the value of another option
    // (`-o /clang:x`), which the driver does not pass through: removing such
    // an argument as well errs on the safe side.
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view argument = args[i];
        for (const std::string_view prefix : clang_cl_pass_through_prefixes) {
            if (argument.starts_with(prefix)) {
                values.emplace_back(argument.substr(prefix.size()));
                positions.push_back(i);
                break;
            }
        }
    }
    Groups groups = removals(values);
    for (std::vector<std::size_t>& group : groups) {
        for (std::size_t& index : group) {
            index = positions[index];
        }
    }
    // An -Xclang outside the pass-through list may forward the argument
    // being removed (`-Xclang /clang:-fplugin=...`).
    add_forwarders(groups, args);
    return groups;
}

} // namespace

FilteredArguments remove_code_loading_arguments(std::vector<std::string> arguments) {
    FilteredArguments result;
    // Repeated until nothing is removed: a removal can change how the
    // arguments around it pair up (a pass-through removal, for one, regroups
    // the arguments outside the pass-through list).
    while (true) {
        Groups groups = removals(arguments);
        if (groups.empty()) {
            groups = pass_through_removals(arguments);
        }
        if (groups.empty()) {
            break;
        }
        std::vector<bool> removed(arguments.size(), false);
        for (const std::vector<std::size_t>& group : groups) {
            std::string joined;
            for (const std::size_t index : group) {
                if (!joined.empty()) {
                    joined += ' ';
                }
                joined += arguments[index];
                removed[index] = true;
            }
            result.removed.push_back(std::move(joined));
        }
        std::vector<std::string> kept;
        kept.reserve(arguments.size());
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (!removed[i]) {
                kept.push_back(std::move(arguments[i]));
            }
        }
        arguments = std::move(kept);
    }
    result.arguments = std::move(arguments);
    return result;
}

} // namespace lcart::analysis
