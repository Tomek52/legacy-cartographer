// Includes every module header, so each one is compiled (and checked by
// clang-tidy) even before it has an implementation.
#include "analysis/analyzer.hpp"
#include "analysis/module_rules.hpp"
#include "claims/claim.hpp"
#include "claims/claim_parser.hpp"
#include "common/result.hpp"
#include "common/version.hpp"
#include "config/path_glob.hpp"
#include "config/project_config.hpp"
#include "facts/fact_export.hpp"
#include "facts/fact_model.hpp"
#include "facts/fact_store.hpp"
#include "facts/sqlite_fact_store.hpp"
#include "handoff/handoff.hpp"
#include "llm/egress_policy.hpp"
#include "llm/provider.hpp"
#include "report/report_writer.hpp"
#include "verify/claim_verifier.hpp"
#include "verify/verdict.hpp"

#include <gtest/gtest.h>

TEST(Headers, VersionIsSet) {
    EXPECT_FALSE(lcart::common::version.empty());
}
