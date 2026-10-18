# The texts a handoff bundle copies from the specification pages (B-30,
# ADR-0028): the prompt and the agent output of docs/specs/handoff-contract.md
# and the JSON Schema of docs/specs/claim-schema.md. Extracted at configure
# time into a generated header, so the pages are the only copy; both pages
# are configure dependencies, so editing one reconfigures and rebuilds lcart.
# Blocks are found by the prefix of their anchor ID, so a version bump of an
# anchor needs no change here. A page without the expected block fails the
# configure step.

# Sets `out_var` to the text of the first fenced block opened by
# "```<language>" after the anchor whose ID starts with `anchor_prefix`,
# without the fences, ending with a newline.
function(_lcart_fenced_block page anchor_prefix language out_var)
    string(FIND "${page}" "<a id=\"${anchor_prefix}" anchor)
    if(anchor EQUAL -1)
        message(FATAL_ERROR "handoff contract texts: no anchor ${anchor_prefix}...")
    endif()
    string(SUBSTRING "${page}" ${anchor} -1 rest)
    set(opening "```${language}\n")
    string(FIND "${rest}" "${opening}" open)
    if(open EQUAL -1)
        message(FATAL_ERROR "handoff contract texts: no ${language} block after ${anchor_prefix}")
    endif()
    string(LENGTH "${opening}" opening_length)
    math(EXPR begin "${open} + ${opening_length}")
    string(SUBSTRING "${rest}" ${begin} -1 rest)
    string(FIND "${rest}" "\n```\n" close)
    if(close EQUAL -1)
        message(FATAL_ERROR "handoff contract texts: unterminated block after ${anchor_prefix}")
    endif()
    math(EXPR length "${close} + 1")
    string(SUBSTRING "${rest}" 0 ${length} block)
    set(${out_var} "${block}" PARENT_SCOPE)
endfunction()

# Sets `out_var` to the section whose `## ` heading starts with the anchor
# `anchor_prefix`, as a document of its own: `# <heading text>`, then the
# section's text up to the next `## ` heading, ending with one newline.
function(_lcart_section_document page anchor_prefix out_var)
    set(heading_start "## <a id=\"${anchor_prefix}")
    string(FIND "${page}" "${heading_start}" anchor)
    if(anchor EQUAL -1)
        message(FATAL_ERROR "handoff contract texts: no section ${anchor_prefix}...")
    endif()
    string(SUBSTRING "${page}" ${anchor} -1 rest)
    string(FIND "${rest}" "\n" end_of_heading)
    string(SUBSTRING "${rest}" 0 ${end_of_heading} heading)
    string(FIND "${heading}" "</a>" title_at)
    if(title_at EQUAL -1)
        message(FATAL_ERROR "handoff contract texts: heading of ${anchor_prefix} has no </a>")
    endif()
    math(EXPR title_at "${title_at} + 4")
    string(SUBSTRING "${heading}" ${title_at} -1 title)
    math(EXPR body_at "${end_of_heading} + 1")
    string(SUBSTRING "${rest}" ${body_at} -1 rest)
    string(FIND "${rest}" "\n## " next)
    if(NOT next EQUAL -1)
        string(SUBSTRING "${rest}" 0 ${next} rest)
    endif()
    string(REGEX REPLACE "\n+$" "" body "${rest}")
    set(${out_var} "# ${title}\n${body}\n" PARENT_SCOPE)
endfunction()

# Writes the header `output` from src/handoff/src/contract_texts.hpp.in.
function(lcart_generate_handoff_texts output)
    set(contract_page "${PROJECT_SOURCE_DIR}/docs/specs/handoff-contract.md")
    set(schema_page "${PROJECT_SOURCE_DIR}/docs/specs/claim-schema.md")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${contract_page}" "${schema_page}")
    file(READ "${contract_page}" contract)
    file(READ "${schema_page}" schema)
    _lcart_fenced_block("${contract}" "claim-handoff-prompt-v" "markdown" LCART_PROMPT_TEMPLATE)
    _lcart_section_document("${contract}" "claim-agent-output-v" LCART_AGENT_OUTPUT)
    _lcart_fenced_block("${schema}" "claim-json-schema-v" "json" LCART_CLAIM_SCHEMA)
    foreach(text IN ITEMS LCART_PROMPT_TEMPLATE LCART_AGENT_OUTPUT LCART_CLAIM_SCHEMA)
        string(FIND "${${text}}" ")lcart_page\"" clash)
        if(NOT clash EQUAL -1)
            message(FATAL_ERROR "handoff contract texts: ${text} contains the raw string delimiter")
        endif()
    endforeach()
    configure_file("${PROJECT_SOURCE_DIR}/src/handoff/src/contract_texts.hpp.in" "${output}" @ONLY)
endfunction()
