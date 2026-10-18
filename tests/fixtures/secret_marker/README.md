# Fixture: secret_marker

A marker string, `LCART-SECRET-MARKER-7d3f`, in every place a facts-only
handoff bundle must not copy from (B-30 criterion 4, NFR-002 criterion 2):
a line comment, a block comment, a string literal and a macro's string
literal (`marker.c`, `marker.h`), and an `#error` (`marker_error.c`), which
fails that translation unit with a diagnostic quoting the marker, so the
marker reaches the fact database (and the facts export) but must still stay
out of the bundle. Two translation units, one of which parses; one
function, `marker_greeting`.
