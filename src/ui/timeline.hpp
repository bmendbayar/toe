#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/fields.hpp"
#include "core/task_id.hpp"
#include "ui/date_util.hpp"

namespace toe::ui
{

// Sentinel column values for tasks that fall outside the visible window.
inline constexpr int COL_BEFORE_WINDOW = -1;
inline constexpr int COL_AFTER_WINDOW = -2;

// A task with a due date, pre-resolved for layout purposes.
struct DatedTask
{
    core::TaskId id;
    std::int64_t due_epoch;
    core::Priority priority;
    Urgency urgency;
    std::string_view description;  // points into the pool's string arena
};

// A single row in the timeline grid.
struct TimelineRow
{
    core::TaskId id;
    std::string_view description;
    core::Priority priority;
    Urgency urgency;
    int col;  // [0, span_days), kColBeforeWindow, or kColAfterWindow
};

// Display color for a timeline bar. Overdue status takes precedence over priority.
enum class RowColor : std::uint8_t
{
    OVERDUE,          // red
    HIGH_PRIORITY,    // bold/magenta
    MEDIUM_PRIORITY,  // yellow
    NORMAL,           // default
};

[[nodiscard]] constexpr RowColor
timeline_row_color(core::Priority p, Urgency u) noexcept
{
    if (u == Urgency::OVERDUE)
    {
        return RowColor::OVERDUE;
    }
    switch (p)
    {
        case core::Priority::HIGH:
            return RowColor::HIGH_PRIORITY;
        case core::Priority::MEDIUM:
            return RowColor::MEDIUM_PRIORITY;
        case core::Priority::LOW:
            return RowColor::NORMAL;
    }
    return RowColor::NORMAL;
}

// Map each DatedTask to its grid column.
// `start_epoch` must be midnight UTC of day 0 (column 0).
// `span_days` is the number of columns; columns are [0, span_days).
// Tasks falling before or after the window receive sentinel column values.
// Preserves the order of `dated_tasks`.
[[nodiscard]] inline std::vector<TimelineRow>
layout_timeline(
    std::span<const DatedTask> dated_tasks,
    std::int64_t start_epoch,
    int span_days
)
{
    std::vector<TimelineRow> rows;
    rows.reserve(dated_tasks.size());

    for (const DatedTask &t : dated_tasks)
    {
        const std::int64_t due_day = floor_to_day(t.due_epoch);
        const std::int64_t start_day = floor_to_day(start_epoch);
        const std::int64_t diff = (due_day - start_day) / SECONDS_PER_DAY;

        int col;
        if (diff < 0)
        {
            col = COL_BEFORE_WINDOW;
        }
        else if (diff >= static_cast<std::int64_t>(span_days))
        {
            col = COL_AFTER_WINDOW;
        }
        else
        {
            col = static_cast<int>(diff);
        }

        rows.push_back(TimelineRow{
            .id = t.id,
            .description = t.description,
            .priority = t.priority,
            .urgency = t.urgency,
            .col = col,
        });
    }

    return rows;
}

}  // namespace toe::ui
