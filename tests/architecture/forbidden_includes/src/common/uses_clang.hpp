// Self-test fixture for the include boundary check (B-04): a Clang header
// outside src/analysis, spelled with spaces after `#` (still a directive).
// Never compiled.
// clang-format off
#  include <clang/AST/Decl.h>
// clang-format on
