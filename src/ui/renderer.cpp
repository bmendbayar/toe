#include <array>
#include <cstdint>
#include <cstdio>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "core/fields.hpp"
#include "ui/date_util.hpp"
#include "ui/renderer.hpp"

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

[[nodiscard]] ftxui::Element render_row(const core::NodePool &pool, const ViewState &state, core::TaskId id)
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
        row_elems.push_back(text("  "));
        row_elems.push_back(color(Color::Cyan, text(format_due_date(*due_opt))));
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
            rows.push_back(render_row(pool, state, id));
        }
        task_list = vbox(std::move(rows));
    }

    if (state.mode == Mode::DatePicker)
    {
        return hbox({std::move(task_list) | flex, render_calendar(state, now_epoch)});
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
            return hbox({text(" INSERT ") | bold | color(Color::BlueLight), text("  "), text(state.edit_buffer), text("█") | blink});
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
