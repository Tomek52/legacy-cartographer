# Fixture: gnu_inline

Two definitions of one function with external linkage in one translation
unit (B-10, ADR-0013): a GNU `extern inline` definition, used only for
inlining, and the out-of-line definition (`pick.c` lines 4 and 5). Each is
a function with its own call: `pick` on line 4 calls `first`, the second
`pick` (stored USR `pick.c#c:@F@pick`) calls `second`.
