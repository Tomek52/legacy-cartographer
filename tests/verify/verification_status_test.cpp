// Verifies: fr011-verification-report-v1 (status names are part of the JSON
// report format, so they must stay stable).
#include "verify/verdict.hpp"

#include <gtest/gtest.h>

namespace {

using lcart::verify::to_string;
using lcart::verify::VerificationStatus;

static_assert(to_string(VerificationStatus::confirmed) == "confirmed");

TEST(VerificationStatus, HasStableLowercaseNames) {
    EXPECT_EQ(to_string(VerificationStatus::confirmed), "confirmed");
    EXPECT_EQ(to_string(VerificationStatus::unverified), "unverified");
    EXPECT_EQ(to_string(VerificationStatus::contradicted), "contradicted");
}

} // namespace
