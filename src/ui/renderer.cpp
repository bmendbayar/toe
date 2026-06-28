#include <cstdint>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <string_view>

#include "core/fields.hpp"
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

    Element row = hbox({text(indent), text(std::string(fold_marker)), text(std::string(status_glyph(status))), text(" "), std::move(prio_elem), text(" "), std::move(desc_elem)});

    if (id == state.cursor)
    {
        row = inverted(std::move(row));
    }

    return row;
}

ftxui::Element render(const core::NodePool &pool, const ViewState &state, const std::vector<core::TaskId> &visible)
{
    using namespace ftxui;

    if (visible.empty())
    {
        return text("No tasks — press 'o' to create one.") | dim;
    }

    Elements rows;
    rows.reserve(visible.size());
    for (const core::TaskId id : visible)
    {
        rows.push_back(render_row(pool, state, id));
    }
    return vbox(std::move(rows));
}

ftxui::Element render_status_bar(const ViewState &state)
{
    using namespace ftxui;

    switch (state.mode)
    {
        case Mode::Normal:
            return hbox({text(" NORMAL ") | bold | color(Color::GreenLight), text("  ")});
        case Mode::Insert:
            return hbox({text(" INSERT ") | bold | color(Color::BlueLight), text("  "), text(state.edit_buffer), text("█") | blink});
        case Mode::Command:
            return hbox({text(":"), text(state.cmd_buffer), text("█") | blink});
    }
    return text("");
}

}  // namespace toe::ui
