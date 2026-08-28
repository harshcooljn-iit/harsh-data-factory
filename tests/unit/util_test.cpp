#include <gtest/gtest.h>

#include "flowforge/util/string_utils.hpp"
#include "flowforge/util/time_utils.hpp"
#include "flowforge/version.hpp"

namespace {

using namespace flowforge;

TEST(StringUtils, TrimRemovesSurroundingWhitespace) {
    EXPECT_EQ(util::trim("  hello \t\n"), "hello");
    EXPECT_EQ(util::trim("nochange"), "nochange");
    EXPECT_EQ(util::trim("   "), "");
    EXPECT_EQ(util::trim(""), "");
}

TEST(StringUtils, SplitPreservesEmptyFields) {
    const auto parts = util::split("a,,b,", ',');
    ASSERT_EQ(parts.size(), 4u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "");
    EXPECT_EQ(parts[2], "b");
    EXPECT_EQ(parts[3], "");
}

TEST(StringUtils, JoinRoundTripsWithSplit) {
    const std::vector<std::string> parts{"x", "y", "z"};
    EXPECT_EQ(util::join(parts, "::"), "x::y::z");
}

TEST(StringUtils, AffixChecks) {
    EXPECT_TRUE(util::starts_with("pipeline.json", "pipeline"));
    EXPECT_TRUE(util::ends_with("pipeline.json", ".json"));
    EXPECT_FALSE(util::ends_with("x", "xxxxx"));
}

TEST(StringUtils, FormatDuration) {
    EXPECT_EQ(util::format_duration(1.234), "1.23s");
    EXPECT_EQ(util::format_duration(-5.0), "0.00s");
    EXPECT_EQ(util::format_duration(65.0), "1m05.0s");
}

TEST(TimeUtils, UnixMillisRoundTrip) {
    const auto tp = util::from_unix_millis(1'724'000'000'123);
    EXPECT_EQ(util::to_unix_millis(tp), 1'724'000'000'123);
}

TEST(TimeUtils, Iso8601Format) {
    const auto tp = util::from_unix_millis(0);
    EXPECT_EQ(util::to_iso8601(tp), "1970-01-01T00:00:00.000Z");
}

TEST(TimeUtils, SecondsBetweenClampsToZero) {
    const auto a = util::from_unix_millis(2000);
    const auto b = util::from_unix_millis(1000);
    EXPECT_DOUBLE_EQ(util::seconds_between(a, b), 0.0);
    EXPECT_DOUBLE_EQ(util::seconds_between(b, a), 1.0);
}

TEST(Version, IsPopulated) {
    EXPECT_GE(version::kMajor, 0);
    EXPECT_STREQ(version::kName, "FlowForge");
    EXPECT_NE(std::string(version::kString).find('.'), std::string::npos);
}

}  // namespace
