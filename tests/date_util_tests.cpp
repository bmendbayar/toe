#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

#include "ui/date_util.hpp"

namespace
{

using namespace toe::ui;

// Build a midnight-UTC epoch for the given calendar date.
[[nodiscard]] std::int64_t e(int y, unsigned m, unsigned d)
{
    using namespace std::chrono;
    return ymd_to_epoch(year_month_day{year{y}, month{m}, day{d}});
}

// A fixed "now": 2024-06-15 (a Saturday) at 12:34:56 UTC.
[[nodiscard]] std::int64_t now_fixture()
{
    return e(2024, 6, 15) + 45296;  // 12:34:56
}

// The calendar math is genuinely constexpr — assert it folds at compile time.
static_assert(add_days(0, 1) == kSecondsPerDay);
static_assert(floor_to_day(kSecondsPerDay + 5) == kSecondsPerDay);
static_assert(floor_to_day(-1) == -kSecondsPerDay);
static_assert(
    add_months(ymd_to_epoch(std::chrono::year{2024} / 1 / 31), 1) == ymd_to_epoch(std::chrono::year{2024} / 2 / 29)
);
static_assert(due_urgency(0, kSecondsPerDay) == Urgency::Overdue);

// --- epoch <-> calendar ------------------------------------------------------

TEST(DateUtil, EpochAnchorMatchesKnownValue)
{
    // 2024-01-01 00:00:00 UTC is 19723 days past the epoch.
    EXPECT_EQ(e(2024, 1, 1), std::int64_t{19723} * kSecondsPerDay);
}

TEST(DateUtil, EpochYmdRoundTrip)
{
    for (const std::int64_t epoch : {e(1970, 1, 1), e(1999, 12, 31), e(2024, 2, 29), e(2100, 7, 4)})
    {
        EXPECT_EQ(ymd_to_epoch(epoch_to_ymd(epoch)), epoch);
    }
}

TEST(DateUtil, FloorToDay)
{
    EXPECT_EQ(floor_to_day(now_fixture()), e(2024, 6, 15));
    EXPECT_EQ(floor_to_day(e(2024, 6, 15)), e(2024, 6, 15));
    // Negative instants floor toward the earlier day.
    EXPECT_EQ(floor_to_day(-1), -kSecondsPerDay);
}

TEST(DateUtil, AddDays)
{
    EXPECT_EQ(add_days(e(2024, 6, 15), 3), e(2024, 6, 18));
    EXPECT_EQ(add_days(e(2024, 6, 15), -20), e(2024, 5, 26));
    EXPECT_EQ(add_days(e(2024, 12, 31), 1), e(2025, 1, 1));
}

TEST(DateUtil, AddMonthsClampsEndOfMonth)
{
    EXPECT_EQ(add_months(e(2024, 1, 31), 1), e(2024, 2, 29));   // leap February
    EXPECT_EQ(add_months(e(2023, 1, 31), 1), e(2023, 2, 28));   // non-leap February
    EXPECT_EQ(add_months(e(2024, 3, 31), -1), e(2024, 2, 29));  // backward clamp
}

TEST(DateUtil, AddMonthsCrossesYear)
{
    EXPECT_EQ(add_months(e(2023, 12, 15), 1), e(2024, 1, 15));
    EXPECT_EQ(add_months(e(2024, 1, 15), -1), e(2023, 12, 15));
    EXPECT_EQ(add_months(e(2024, 6, 15), 12), e(2025, 6, 15));
}

TEST(DateUtil, MonthGridLeapFebruary)
{
    using namespace std::chrono;
    const MonthGrid g = month_grid(year{2024} / February);
    EXPECT_EQ(g.days_in_month, 29u);
    EXPECT_EQ(g.first_weekday, 4u);  // 2024-02-01 is a Thursday (Sun=0)
}

TEST(DateUtil, MonthGridNonLeapFebruary)
{
    using namespace std::chrono;
    const MonthGrid g = month_grid(year{2023} / February);
    EXPECT_EQ(g.days_in_month, 28u);
    EXPECT_EQ(g.first_weekday, 3u);  // 2023-02-01 is a Wednesday
}

// --- formatting --------------------------------------------------------------

TEST(DateUtil, FormatDueDate)
{
    EXPECT_EQ(format_due_date(e(2024, 6, 15)), "2024-06-15");
    EXPECT_EQ(format_due_date(e(1999, 1, 2)), "1999-01-02");
}

TEST(DateUtil, FormatRelativeNearLabels)
{
    const std::int64_t now = now_fixture();
    EXPECT_EQ(format_relative(e(2024, 6, 15), now), "today");
    EXPECT_EQ(format_relative(e(2024, 6, 16), now), "tomorrow");
    EXPECT_EQ(format_relative(e(2024, 6, 14), now), "yesterday");
}

TEST(DateUtil, FormatRelativeDaysAndWeeks)
{
    const std::int64_t now = now_fixture();
    EXPECT_EQ(format_relative(e(2024, 6, 18), now), "in 3d");
    EXPECT_EQ(format_relative(e(2024, 6, 10), now), "5d overdue");
    EXPECT_EQ(format_relative(e(2024, 7, 1), now), "in 2w");        // 16 days -> 2w
    EXPECT_EQ(format_relative(e(2024, 5, 15), now), "4w overdue");  // 31 days -> 4w
}

// --- urgency -----------------------------------------------------------------

TEST(DateUtil, UrgencyClassification)
{
    const std::int64_t now = now_fixture();
    EXPECT_EQ(due_urgency(e(2024, 6, 14), now), Urgency::Overdue);
    EXPECT_EQ(due_urgency(e(2024, 6, 15), now), Urgency::Soon);  // today counts
    EXPECT_EQ(due_urgency(e(2024, 6, 18), now), Urgency::Soon);  // boundary (3 days)
    EXPECT_EQ(due_urgency(e(2024, 6, 19), now), Urgency::Normal);
}

TEST(DateUtil, UrgencyIgnoresTimeOfDay)
{
    // A due date stored late in the day is still "today", not overdue.
    const std::int64_t now = e(2024, 6, 15) + 80000;  // late evening
    EXPECT_EQ(due_urgency(e(2024, 6, 15) + 100, now), Urgency::Soon);
}

// --- absolute parsing --------------------------------------------------------

TEST(DateUtil, ParseDueDateValid)
{
    EXPECT_EQ(parse_due_date("2024-06-15"), e(2024, 6, 15));
}

TEST(DateUtil, ParseDueDateRejectsMalformed)
{
    EXPECT_FALSE(parse_due_date("").has_value());
    EXPECT_FALSE(parse_due_date("2024/06/15").has_value());
    EXPECT_FALSE(parse_due_date("2024-6-15").has_value());
    EXPECT_FALSE(parse_due_date("2024-13-01").has_value());  // bad month
    EXPECT_FALSE(parse_due_date("2024-02-30").has_value());  // bad day
    EXPECT_FALSE(parse_due_date("20x4-06-15").has_value());  // non-digit
}

// --- relative parsing --------------------------------------------------------

TEST(DateUtil, ParseRelativeNamed)
{
    const std::int64_t now = now_fixture();
    EXPECT_EQ(parse_relative_date("today", now), e(2024, 6, 15));
    EXPECT_EQ(parse_relative_date("tomorrow", now), e(2024, 6, 16));
    EXPECT_EQ(parse_relative_date("yesterday", now), e(2024, 6, 14));
    EXPECT_EQ(parse_relative_date("eom", now), e(2024, 6, 30));
}

TEST(DateUtil, ParseRelativeWeekday)
{
    const std::int64_t now = now_fixture();                      // 2024-06-15 is a Saturday
    EXPECT_EQ(parse_relative_date("sat", now), e(2024, 6, 15));  // today counts
    EXPECT_EQ(parse_relative_date("sun", now), e(2024, 6, 16));
    EXPECT_EQ(parse_relative_date("fri", now), e(2024, 6, 21));  // soonest upcoming
}

TEST(DateUtil, ParseRelativeOffsets)
{
    const std::int64_t now = now_fixture();
    EXPECT_EQ(parse_relative_date("+3d", now), e(2024, 6, 18));
    EXPECT_EQ(parse_relative_date("-5d", now), e(2024, 6, 10));
    EXPECT_EQ(parse_relative_date("+2w", now), e(2024, 6, 29));
    EXPECT_EQ(parse_relative_date("+1m", now), e(2024, 7, 15));
    EXPECT_EQ(parse_relative_date("-1m", now), e(2024, 5, 15));
}

TEST(DateUtil, ParseRelativeRejectsMalformed)
{
    const std::int64_t now = now_fixture();
    EXPECT_FALSE(parse_relative_date("", now).has_value());
    EXPECT_FALSE(parse_relative_date("garbage", now).has_value());
    EXPECT_FALSE(parse_relative_date("3d", now).has_value());    // sign required
    EXPECT_FALSE(parse_relative_date("+d", now).has_value());    // no digits
    EXPECT_FALSE(parse_relative_date("+3x", now).has_value());   // bad unit
    EXPECT_FALSE(parse_relative_date("+3dd", now).has_value());  // trailing char
    EXPECT_FALSE(parse_relative_date("+3", now).has_value());    // no unit
}

// --- unified entry point -----------------------------------------------------

TEST(DateUtil, ParseDateInputDispatches)
{
    const std::int64_t now = now_fixture();
    EXPECT_EQ(parse_date_input("2024-06-18", now), e(2024, 6, 18));  // absolute
    EXPECT_EQ(parse_date_input("+3d", now), e(2024, 6, 18));         // relative
    EXPECT_EQ(parse_date_input("today", now), e(2024, 6, 15));
    EXPECT_FALSE(parse_date_input("nonsense", now).has_value());
}

}  // namespace
