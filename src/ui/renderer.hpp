#pragma once

#include <ftxui/dom/elements.hpp>
#include <vector>

#include "core/node_pool.hpp"
#include "core/task_id.hpp"
#include "ui/view_state.hpp"

namespace toe::ui
{

// Produce the main task-list Element (plus calendar panel when in DatePicker
// mode). Pure: reads Core + ViewState, no side-effects. `now_epoch` is used
// to highlight today in the calendar and will be used for urgency coloring in M9.
[[nodiscard]] ftxui::Element render(
    const core::NodePool &pool,
    const ViewState &state,
    const std::vector<core::TaskId> &visible,
    std::int64_t now_epoch
);

// Status bar: mode indicator, command buffer, or edit prompt.
[[nodiscard]] ftxui::Element render_status_bar(const ViewState &state);

// Calendar month grid for the DatePicker mode.
[[nodiscard]] ftxui::Element render_calendar(const ViewState &state, std::int64_t now_epoch);

}  // namespace toe::ui
