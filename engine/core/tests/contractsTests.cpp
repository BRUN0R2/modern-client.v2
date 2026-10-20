// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

import std;
import nx.core;

#include "contractRecorder.hpp"

namespace {

[[nodiscard]] auto halve(int value) noexcept -> int {
    nx::expects(value % 2 == 0, "halve: value must be even");
    return value / 2;
}

} // namespace

TEST(Contracts, SatisfiedConditionsReportNothing) {
    const nx_test::ContractRecorder recorder;

    nx::expects(true, "always holds");
    nx::ensures(true, "always holds");
    nx::invariant(true, "always holds");
    EXPECT_EQ(halve(8), 4);

    EXPECT_EQ(recorder.count(), 0);
}

TEST(Contracts, ViolationReachesTheHandlerWithKindDescriptionAndLocation) {
    const nx_test::ContractRecorder recorder;

    std::ignore = halve(3);

    ASSERT_EQ(recorder.count(), 1);
    EXPECT_EQ(recorder.last().kind, nx::ContractKind::Precondition);
    EXPECT_EQ(recorder.last().description, "halve: value must be even");
    EXPECT_TRUE(std::string_view{recorder.last().where.function_name()}.contains("halve"));
}

TEST(Contracts, EachCheckReportsItsOwnKind) {
    const nx_test::ContractRecorder recorder;

    nx::ensures(false, "postcondition");
    EXPECT_EQ(recorder.last().kind, nx::ContractKind::Postcondition);

    nx::invariant(false, "invariant");
    EXPECT_EQ(recorder.last().kind, nx::ContractKind::Invariant);

    EXPECT_EQ(recorder.count(), 2);
}

TEST(Contracts, ObservingHandlerLetsExecutionContinue) {
    const nx_test::ContractRecorder recorder;

    const auto result = halve(5);

    EXPECT_EQ(result, 2);
    EXPECT_EQ(recorder.count(), 1);
}

TEST(ContractsDeathTest, DefaultHandlerReportsAndAborts) {
    EXPECT_DEATH(nx::expects(false, "map must be loaded"), "Precondition violated: map must be loaded");
}

TEST(Contracts, NullHandlerRestoresTheEnforcingDefault) {
    const auto previous = nx::setContractViolationHandler(nullptr);
    const auto installed = nx::setContractViolationHandler(previous);

    EXPECT_EQ(installed, &nx::enforceContract);
}
