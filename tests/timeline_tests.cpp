#include <gtest/gtest.h>

#include "core/fields.hpp"
#include "core/task_id.hpp"
#include "ui/date_util.hpp"
#include "ui/timeline.hpp"

namespace toe::ui
{

using core::Priority;
using core::TaskId;

// Helper: build a DatedTask with a due date N days from start_epoch.
static DatedTask make_task(std::int64_t start_epoch, int offset_days, Priority p, Urgency u)
{
    return DatedTask{
        .id = TaskId::invalid(),
        .due_epoch = add_days(start_epoch, offset_days),
        .priority = p,
        .urgency = u,
        .description = "task",
    };
}

// Fixed reference start: 2024-06-28 midnight UTC.
static const std::int64_t kStart = ymd_to_epoch(
    std::chrono::year_month_day{std::chrono::year{2024}, std::chrono::month{6}, std::chrono::day{28}}
);

// --- layout_timeline: date-to-column mapping ---------------------------------

TEST(Timeline, DateOnStartIsDayZero)
{
    const DatedTask t = make_task(kStart, 0, Priority::Low, Urgency::Normal);
    const auto rows = layout_timeline({&t, 1}, kStart, 7);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].col, 0);
}

TEST(Timeline, DateOnLastDayIsSpanMinusOne)
{
    const DatedTask t = make_task(kStart, 6, Priority::Low, Urgency::Normal);
    const auto rows = layout_timeline({&t, 1}, kStart, 7);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].col, 6);
}

TEST(Timeline, MultipleDatesMapToCorrectColumns)
{
    const DatedTask tasks[] = {
        make_task(kStart, 0, Priority::Low, Urgency::Normal),
        make_task(kStart, 3, Priority::Low, Urgency::Normal),
        make_task(kStart, 6, Priority::Low, Urgency::Normal),
    };
    const auto rows = layout_timeline(tasks, kStart, 7);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].col, 0);
    EXPECT_EQ(rows[1].col, 3);
    EXPECT_EQ(rows[2].col, 6);
}

TEST(Timeline, PreservesInputOrder)
{
    const DatedTask tasks[] = {
        make_task(kStart, 5, Priority::Low, Urgency::Normal),
        make_task(kStart, 1, Priority::Low, Urgency::Normal),
        make_task(kStart, 3, Priority::Low, Urgency::Normal),
    };
    const auto rows = layout_timeline(tasks, kStart, 7);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].col, 5);
    EXPECT_EQ(rows[1].col, 1);
    EXPECT_EQ(rows[2].col, 3);
}

// --- layout_timeline: off-window clamping ------------------------------------

TEST(Timeline, BeforeWindowReturnsColBeforeWindow)
{
    const DatedTask t = make_task(kStart, -1, Priority::Low, Urgency::Overdue);
    const auto rows = layout_timeline({&t, 1}, kStart, 7);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].col, kColBeforeWindow);
}

TEST(Timeline, AfterWindowReturnsColAfterWindow)
{
    const DatedTask t = make_task(kStart, 7, Priority::Low, Urgency::Normal);
    const auto rows = layout_timeline({&t, 1}, kStart, 7);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].col, kColAfterWindow);
}

TEST(Timeline, FarBeforeWindowIsClamped)
{
    const DatedTask t = make_task(kStart, -100, Priority::Low, Urgency::Overdue);
    const auto rows = layout_timeline({&t, 1}, kStart, 14);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].col, kColBeforeWindow);
}

TEST(Timeline, FarAfterWindowIsClamped)
{
    const DatedTask t = make_task(kStart, 100, Priority::Low, Urgency::Normal);
    const auto rows = layout_timeline({&t, 1}, kStart, 14);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].col, kColAfterWindow);
}

TEST(Timeline, EmptyInputReturnsEmpty)
{
    const auto rows = layout_timeline({}, kStart, 7);
    EXPECT_TRUE(rows.empty());
}

TEST(Timeline, MixedWindowClamping)
{
    const DatedTask tasks[] = {
        make_task(kStart, -2, Priority::Low, Urgency::Overdue),  // before
        make_task(kStart, 0, Priority::Low, Urgency::Normal),    // in window
        make_task(kStart, 6, Priority::Low, Urgency::Normal),    // in window (last)
        make_task(kStart, 7, Priority::Low, Urgency::Normal),    // after
    };
    const auto rows = layout_timeline(tasks, kStart, 7);
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].col, kColBeforeWindow);
    EXPECT_EQ(rows[1].col, 0);
    EXPECT_EQ(rows[2].col, 6);
    EXPECT_EQ(rows[3].col, kColAfterWindow);
}

// --- timeline_row_color: priority / urgency selection ------------------------

TEST(TimelineRowColor, OverdueTakesPrecedenceOverLowPriority)
{
    EXPECT_EQ(timeline_row_color(Priority::Low, Urgency::Overdue), RowColor::Overdue);
}

TEST(TimelineRowColor, OverdueTakesPrecedenceOverHighPriority)
{
    EXPECT_EQ(timeline_row_color(Priority::High, Urgency::Overdue), RowColor::Overdue);
}

TEST(TimelineRowColor, OverdueTakesPrecedenceOverMediumPriority)
{
    EXPECT_EQ(timeline_row_color(Priority::Medium, Urgency::Overdue), RowColor::Overdue);
}

TEST(TimelineRowColor, HighPriorityNormal)
{
    EXPECT_EQ(timeline_row_color(Priority::High, Urgency::Normal), RowColor::HighPriority);
}

TEST(TimelineRowColor, HighPrioritySoon)
{
    EXPECT_EQ(timeline_row_color(Priority::High, Urgency::Soon), RowColor::HighPriority);
}

TEST(TimelineRowColor, MediumPriorityNormal)
{
    EXPECT_EQ(timeline_row_color(Priority::Medium, Urgency::Normal), RowColor::MediumPriority);
}

TEST(TimelineRowColor, MediumPrioritySoon)
{
    EXPECT_EQ(timeline_row_color(Priority::Medium, Urgency::Soon), RowColor::MediumPriority);
}

TEST(TimelineRowColor, LowPriorityNormalIsNormalColor)
{
    EXPECT_EQ(timeline_row_color(Priority::Low, Urgency::Normal), RowColor::Normal);
}

TEST(TimelineRowColor, LowPrioritySoonIsNormalColor)
{
    EXPECT_EQ(timeline_row_color(Priority::Low, Urgency::Soon), RowColor::Normal);
}

// --- TimelineRow carries correct metadata ------------------------------------

TEST(Timeline, RowCarriesDescriptionPriorityUrgency)
{
    DatedTask t{
        .id = TaskId::invalid(),
        .due_epoch = add_days(kStart, 2),
        .priority = Priority::High,
        .urgency = Urgency::Soon,
        .description = "important task",
    };
    const auto rows = layout_timeline({&t, 1}, kStart, 7);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].col, 2);
    EXPECT_EQ(rows[0].priority, Priority::High);
    EXPECT_EQ(rows[0].urgency, Urgency::Soon);
    EXPECT_EQ(rows[0].description, "important task");
}

}  // namespace toe::ui
