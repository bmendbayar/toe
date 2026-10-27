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
// The tree list is drawn starting at visible[state.tree_scroll].
// When state.show_details is set, the cursor task's description pane sits to
// the right (or below, under NARROW_COLS) of the main view.
[[nodiscard]] ftxui::Element render(
    const core::NodePool &pool,
    const ViewState &state,
    const std::vector<core::TaskId> &visible,
    std::int64_t now_epoch,
    int screen_cols = 120
);

// Terminals narrower than this stack the description pane below the list.
inline constexpr int NARROW_COLS = 80;
// Fixed height of the stacked pane (borders included).
inline constexpr int DETAILS_PANE_ROWS = 10;

// Rows the stacked description pane takes away from the main view, so the
// caller can size the tree viewport; 0 when the pane is hidden or beside it.
[[nodiscard]] int details_pane_rows(const ViewState &state, int screen_cols);

// Bordered pane: the cursor task's title, then its wrapped description.
[[nodiscard]] ftxui::Element render_details(const core::NodePool &pool, const ViewState &state);

// Status bar: mode indicator, command buffer, or edit prompt.
[[nodiscard]] ftxui::Element render_status_bar(const ViewState &state);

// Calendar month grid for the DatePicker mode.
[[nodiscard]] ftxui::Element render_calendar(const ViewState &state, std::int64_t now_epoch);

}  // namespace toe::ui
