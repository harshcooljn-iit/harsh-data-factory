#include <gtest/gtest.h>

#include <algorithm>

#include "flowforge/process/process_spec.hpp"

namespace {

using namespace flowforge::process;

TEST(ProcessSpec, ValidateCatchesEmptyProgramAndArgv) {
    ProcessSpec s;
    EXPECT_TRUE(s.validate().has_value());
    s.program = "python3";
    EXPECT_TRUE(s.validate().has_value());  // argv still empty
    s.argv = {"python3"};
    EXPECT_FALSE(s.validate().has_value());
    s.timeout = std::chrono::milliseconds{-1};
    EXPECT_TRUE(s.validate().has_value());
}

TEST(ProcessSpec, EnvironmentBlockAppliesOverridesAndKeepsOrder) {
    const std::vector<EnvEntry> overrides{{"FF_TEST_A", "1"}, {"FF_TEST_B", "2"}};
    const auto block = build_environment_block(overrides, /*inherit_current=*/false);
    ASSERT_EQ(block.size(), 2u);
    EXPECT_EQ(block[0], "FF_TEST_A=1");
    EXPECT_EQ(block[1], "FF_TEST_B=2");
}

TEST(ProcessSpec, EnvironmentBlockOverridesInheritedValueInPlace) {
    ::setenv("FF_SPEC_TEST_VAR", "inherited", 1);
    const auto block =
        build_environment_block({{"FF_SPEC_TEST_VAR", "overridden"}}, /*inherit_current=*/true);
    const auto it = std::find(block.begin(), block.end(), "FF_SPEC_TEST_VAR=overridden");
    EXPECT_NE(it, block.end());
    EXPECT_EQ(std::count_if(block.begin(), block.end(),
                            [](const std::string& e) {
                                return e.rfind("FF_SPEC_TEST_VAR=", 0) == 0;
                            }),
              1);
    ::unsetenv("FF_SPEC_TEST_VAR");
}

TEST(ProcessSpec, EnvironmentBlockSkipsMalformedKeys) {
    const auto block = build_environment_block({{"", "x"}, {"BAD=KEY", "y"}, {"OK", "z"}},
                                               /*inherit_current=*/false);
    ASSERT_EQ(block.size(), 1u);
    EXPECT_EQ(block[0], "OK=z");
}

}  // namespace
