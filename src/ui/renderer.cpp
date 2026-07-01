#include <array>
#include <cstdint>
#include <cstdio>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/fields.hpp"
#include "ui/date_util.hpp"
#include "ui/renderer.hpp"
#include "ui/timeline.hpp"

namespace toe::ui
{

int task_depth(const core::NodePool &pool, core::TaskId id)
{
    int depth = 0;
    while (true)
    {
        const auto p = pool.parent(id);
        if (!p.has_value() || !p->is_valid())
        {
            break;
        }
        id = *p;
        ++depth;
    }
    return depth;
}

[[nodiscard]] std::string_view status_glyph(core::Status s)
{
    switch (s)
    {
        case core::Status::Todo:
            return "[ ]";
        case core::Status::InProgress:
            return "[>]";
        case core::Status::Done:
            return "[x]";
    }
    return "[ ]";
}

[[nodiscard]] ftxui::Element render_row(
    const core::NodePool &pool,
    const ViewState &state,
    core::TaskId id,
    std::int64_t now_epoch
)
{
    using namespace ftxui;

    const int depth = task_depth(pool, id);
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');

    const auto status = pool.get_status(id).value_or(core::Status::Todo);
    const auto priority = pool.get_priority(id).value_or(core::Priority::Low);
    const auto desc_sv = pool.get_description(id).value_or(std::string_view{});

    const bool has_children =
        pool.first_child(id).value_or(core::TaskId::invalid()).is_valid();
    const bool is_folded = state.folded.contains(id);
    const std::string_view fold_marker =
        has_children ? (is_folded ? "▶ " : "▼ ") : "  ";

    Element desc_elem = text(desc_sv.empty() ? "<no description>" : std::string(desc_sv));
    if (desc_sv.empty() || status == core::Status::Done)
    {
        desc_elem = dim(std::move(desc_elem));
    }
    if (status == core::Status::Done)
    {
        desc_elem = strikethrough(std::move(desc_elem));
    }

    Element prio_elem = [&]() -> Element {
        switch (priority)
        {
            case core::Priority::High:
                return bold(color(Color::Red, text("!!")));
            case core::Priority::Medium:
                return color(Color::Yellow, text("! "));
            case core::Priority::Low:
                return text("  ");
        }
        return text("  ");
    }();

    const auto due_opt = pool.get_due_date(id).value_or(std::nullopt);

    Elements row_elems{
        text(indent),
        text(std::string(fold_marker)),
        text(std::string(status_glyph(status))),
        text(" "),
        std::move(prio_elem),
        text(" "),
        std::move(desc_elem),
    };
    if (due_opt.has_value())
    {
        const Urgency urg = due_urgency(*due_opt, now_epoch);
        const std::string label = format_due_date(*due_opt) + " " + format_relative(*due_opt, now_epoch);
        Element date_elem;
        switch (urg)
        {
            case Urgency::Overdue:
                date_elem = color(Color::Red, text(label));
                break;
            case Urgency::Soon:
                date_elem = color(Color::Yellow, text(label));
                break;
            case Urgency::Normal:
                date_elem = color(Color::Cyan, text(label));
                break;
        }
        row_elems.push_back(text("  "));
        row_elems.push_back(std::move(date_elem));
    }
    Element row = hbox(std::move(row_elems));

    if (id == state.cursor)
    {
        row = inverted(std::move(row));
    }

    return row;
}

ftxui::Element render_calendar(const ViewState &state, std::int64_t now_epoch)
{
    using namespace ftxui;
    using namespace std::chrono;

    const auto sel_ymd = epoch_to_ymd(state.picker.view_epoch);
    const year_month ym{sel_ymd.year(), sel_ymd.month()};
    const MonthGrid grid = month_grid(ym);
    const std::int64_t today_ep = today_epoch(now_epoch);
    const std::int64_t sel_ep = floor_to_day(state.picker.view_epoch);

    static constexpr std::array<std::string_view, 12> kMonthNames = {
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December",
    };
    const auto mi = static_cast<unsigned>(ym.month()) - 1;
    char hdr[32];
    std::snprintf(hdr, sizeof(hdr), "%s %d", kMonthNames[mi].data(), static_cast<int>(ym.year()));

    Elements rows;
    rows.push_back(text(hdr) | bold | center);
    // Header row matches cell format: each cell is 3 chars (" XX")
    rows.push_back(text(" Su Mo Tu We Th Fr Sa"));

    // Seed the first week row with empty cells for days before the 1st
    Elements week;
    unsigned col = grid.first_weekday;
    for (unsigned i = 0; i < col; ++i)
    {
        week.push_back(text("   "));
    }

    for (unsigned d = 1; d <= grid.days_in_month; ++d)
    {
        const year_month_day day_ymd{ym.year(), ym.month(), day{d}};
        const std::int64_t day_ep = ymd_to_epoch(day_ymd);

        char buf[4];
        std::snprintf(buf, sizeof(buf), " %2u", d);
        Element cell = text(std::string(buf));

        if (day_ep == sel_ep)
        {
            cell = inverted(std::move(cell));
        }
        else if (day_ep == today_ep)
        {
            cell = bold(color(Color::GreenLight, std::move(cell)));
        }

        week.push_back(std::move(cell));

        if (col == 6)
        {
            rows.push_back(hbox(std::move(week)));
            week.clear();
            col = 0;
        }
        else
        {
            ++col;
        }
    }
    if (!week.empty())
    {
        rows.push_back(hbox(std::move(week)));
    }

    return vbox(std::move(rows)) | border;
}

// Number of day-columns shown in the timeline panel.
inline constexpr int kTimelineSpanDays = 14;

// Width of each day cell in the timeline grid (chars): " DD " = 4.
inline constexpr int kTimelineCellWidth = 4;

// Width of the description column in the timeline panel (chars).
inline constexpr int kTimelineDescWidth = 18;

[[nodiscard]] ftxui::Color row_color_to_ftxui(RowColor rc)
{
    using ftxui::Color;
    switch (rc)
    {
        case RowColor::Overdue:
            return Color::Red;
        case RowColor::HighPriority:
            return Color::Magenta;
        case RowColor::MediumPriority:
            return Color::Yellow;
        case RowColor::Normal:
            return Color::Default;
    }
    return Color::Default;
}

[[nodiscard]] ftxui::Element render_timeline(
    const core::NodePool &pool,
    const ViewState &state,
    const std::vector<core::TaskId> &visible,
    std::int64_t now_epoch
)
{
    using namespace ftxui;
    using namespace std::chrono;

    // Collect dated tasks from the visible set.
    std::vector<DatedTask> dated;
    dated.reserve(visible.size());
    for (const core::TaskId id : visible)
    {
        const auto due_opt = pool.get_due_date(id).value_or(std::nullopt);
        if (!due_opt.has_value())
        {
            continue;
        }
        const auto prio = pool.get_priority(id).value_or(core::Priority::Low);
        const auto urg = due_urgency(*due_opt, now_epoch);
        const auto desc = pool.get_description(id).value_or(std::string_view{});
        dated.push_back(DatedTask{.id = id, .due_epoch = *due_opt, .priority = prio, .urgency = urg, .description = desc});
    }

    const std::vector<TimelineRow> rows =
        layout_timeline(dated, state.timeline_start_epoch, kTimelineSpanDays);

    // --- date axis header -------------------------------------------------------
    // First row: month labels spanning their days.
    // Second row: day-of-month numbers.
    Elements day_header;
    Elements month_header;
    day_header.reserve(kTimelineSpanDays);

    // Track month changes to build the month label row.
    int cur_month_col = 0;
    int cur_month_span = 0;
    std::string cur_month_label;

    const auto flush_month = [&]() {
        if (cur_month_span == 0)
        {
            return;
        }
        // Pad or truncate month label to fit span * cell_width chars.
        std::string label = cur_month_label;
        const int avail = cur_month_span * kTimelineCellWidth;
        if (static_cast<int>(label.size()) > avail)
        {
            label.resize(static_cast<std::size_t>(avail));
        }
        while (static_cast<int>(label.size()) < avail)
        {
            label += ' ';
        }
        (void)cur_month_col;  // informational; unused in rendering
        month_header.push_back(text(label));
        cur_month_span = 0;
    };

    static constexpr std::array<std::string_view, 12> kMonthAbbr = {
        "Jan",
        "Feb",
        "Mar",
        "Apr",
        "May",
        "Jun",
        "Jul",
        "Aug",
        "Sep",
        "Oct",
        "Nov",
        "Dec",
    };

    for (int i = 0; i < kTimelineSpanDays; ++i)
    {
        const std::int64_t day_ep = add_days(state.timeline_start_epoch, i);
        const auto ymd = epoch_to_ymd(day_ep);
        const auto mo = static_cast<unsigned>(ymd.month());
        const auto dy = static_cast<unsigned>(ymd.day());
        const std::string mo_label(kMonthAbbr[mo - 1]);

        if (mo_label != cur_month_label)
        {
            flush_month();
            cur_month_label = mo_label;
            cur_month_col = i;
        }
        ++cur_month_span;

        // Day cell: " DD" right-padded to kTimelineCellWidth.
        char buf[kTimelineCellWidth + 1];
        std::snprintf(buf, sizeof(buf), " %2u ", dy);
        const std::int64_t today_ep = today_epoch(now_epoch);
        Element cell = text(std::string(buf));
        if (floor_to_day(day_ep) == today_ep)
        {
            cell = bold(color(Color::GreenLight, std::move(cell)));
        }
        day_header.push_back(std::move(cell));
    }
    flush_month();

    // Description column header (blank, same width as desc column).
    const std::string desc_pad(kTimelineDescWidth + 2, ' ');

    Elements header_rows;
    header_rows.push_back(hbox({text(desc_pad), hbox(std::move(month_header))}));
    header_rows.push_back(hbox({text(desc_pad), hbox(std::move(day_header))}));

    // --- task rows -------------------------------------------------------------
    Elements task_rows;
    task_rows.reserve(rows.size());

    for (const TimelineRow &row : rows)
    {
        // Description: fixed-width column, truncate with '…' if too long.
        std::string desc_str(row.description);
        if (static_cast<int>(desc_str.size()) > kTimelineDescWidth)
        {
            desc_str.resize(static_cast<std::size_t>(kTimelineDescWidth - 1));
            desc_str += '\xe2';
            desc_str += '\x80';
            desc_str += '\xa6';  // UTF-8 '…'
        }
        while (static_cast<int>(desc_str.size()) < kTimelineDescWidth)
        {
            desc_str += ' ';
        }

        const Color bar_color = row_color_to_ftxui(timeline_row_color(row.priority, row.urgency));

        // Build the bar: spaces up to col, marker at col.
        Elements bar_elems;
        bar_elems.reserve(kTimelineSpanDays + 2);

        if (row.col == kColBeforeWindow)
        {
            // Deadline is left of the window: show a left-arrow indicator.
            bar_elems.push_back(color(bar_color, text("◀")));
            for (int c = 0; c < kTimelineSpanDays; ++c)
            {
                bar_elems.push_back(text(std::string(kTimelineCellWidth, ' ')));
            }
        }
        else if (row.col == kColAfterWindow)
        {
            // Deadline is right of the window: fill all cells with dashes, show arrow.
            for (int c = 0; c < kTimelineSpanDays; ++c)
            {
                bar_elems.push_back(color(bar_color, text(std::string(kTimelineCellWidth, '-'))));
            }
            bar_elems.push_back(color(bar_color, text("▶")));
        }
        else
        {
            // Deadline is inside the window.
            for (int c = 0; c < kTimelineSpanDays; ++c)
            {
                if (c < row.col)
                {
                    bar_elems.push_back(color(bar_color, text(std::string(kTimelineCellWidth, '-'))));
                }
                else if (c == row.col)
                {
                    // Centre the bullet in the cell.
                    bar_elems.push_back(color(bar_color, text(" \xe2\x97\x8f  ")));  // " ●  "
                }
                else
                {
                    bar_elems.push_back(text(std::string(kTimelineCellWidth, ' ')));
                }
            }
        }

        Element task_row = hbox({
            text("  "),
            text(desc_str),
            hbox(std::move(bar_elems)),
        });

        if (row.id == state.cursor)
        {
            task_row = inverted(std::move(task_row));
        }

        task_rows.push_back(std::move(task_row));
    }

    if (task_rows.empty())
    {
        task_rows.push_back(text("  No tasks with due dates.") | dim);
    }

    // Scroll hint footer.
    Element footer = text("  [ scroll left   ] scroll right   T tree view") | dim;

    Elements panel;
    panel.reserve(header_rows.size() + task_rows.size() + 1);
    for (auto &h : header_rows) panel.push_back(std::move(h));
    for (auto &r : task_rows) panel.push_back(std::move(r));
    panel.push_back(std::move(footer));

    return vbox(std::move(panel));
}

ftxui::Element render(
    const core::NodePool &pool,
    const ViewState &state,
    const std::vector<core::TaskId> &visible,
    std::int64_t now_epoch
)
{
    using namespace ftxui;

    Element task_list;
    if (visible.empty())
    {
        task_list = text("No tasks — press 'o' to create one.") | dim;
    }
    else
    {
        Elements rows;
        rows.reserve(visible.size());
        for (const core::TaskId id : visible)
        {
            rows.push_back(render_row(pool, state, id, now_epoch));
        }
        task_list = vbox(std::move(rows));
    }

    if (state.mode == Mode::DatePicker)
    {
        return hbox(
            {std::move(task_list) | flex,
             vbox({render_calendar(state, now_epoch), filler()})}
        );
    }

    if (state.view_mode == ViewMode::Timeline)
    {
        return render_timeline(pool, state, visible, now_epoch);
    }

    return task_list;
}

ftxui::Element render_status_bar(const ViewState &state)
{
    using namespace ftxui;

    switch (state.mode)
    {
        case Mode::Normal:
            return hbox({text(" NORMAL ") | bold | color(Color::GreenLight), text("  ")});
        case Mode::Insert:
            if (state.edit_field == EditField::DueDate)
            {
                return hbox(
                    {text(" DATE   ") | bold | color(Color::Cyan),
                     text("  YYYY-MM-DD  "),
                     text(state.edit_buffer),
                     text("█") | blink}
                );
            }
            return hbox(
                {text(" INSERT ") | bold | color(Color::BlueLight),
                 text("  "),
                 text(state.edit_buffer),
                 text("█") | blink}
            );
        case Mode::Command:
            return hbox({text(":"), text(state.cmd_buffer), text("█") | blink});
        case Mode::DatePicker:
            return hbox({
                text(" PICK   ") | bold | color(Color::Magenta),
                text("  h/l:day  j/k:week  [/]:month  t:today  Enter:set  d:clear  i:type  Esc:cancel"),
            });
    }
    return text("");
}

}  // namespace toe::ui
