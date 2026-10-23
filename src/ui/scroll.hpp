#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "core/task_id.hpp"
#include "ui/view_state.hpp"

namespace toe::ui
{

// Rows kept between the cursor and the viewport edge while scrolling.
inline constexpr std::size_t SCROLL_OFF = 3;

// Returns the new top-row index such that `cursor_idx` stays at least
// `scrolloff` rows from either edge (scrolloff shrinks to fit tiny viewports),
// and the view never leaves blank rows below the last task.
// `cursor_idx >= total` means the cursor is absent: only clamp.
[[nodiscard]] constexpr std::size_t scroll_into_view(
    std::size_t top,
    std::size_t cursor_idx,
    std::size_t total,
    std::size_t viewport_rows,
    std::size_t scrolloff = SCROLL_OFF
)
{
    if (viewport_rows == 0 || total <= viewport_rows)
    {
        return 0;
    }

    const std::size_t so = std::min(scrolloff, (viewport_rows - 1) / 2);

    if (cursor_idx < total)
    {
        if (cursor_idx < top + so)
        {
            top = cursor_idx - std::min(cursor_idx, so);
        }
        else if (cursor_idx + so >= top + viewport_rows)
        {
            top = cursor_idx + so + 1 - viewport_rows;
        }
    }

    return std::min(top, total - viewport_rows);
}

// Recompute state.tree_scroll for the current cursor, list and viewport height.
inline void update_tree_scroll(
    ViewState &state,
    const std::vector<core::TaskId> &visible,
    std::size_t viewport_rows
)
{
    const auto it = std::ranges::find(visible, state.cursor);
    const auto cursor_idx = static_cast<std::size_t>(it - visible.begin());
    state.tree_scroll =
        scroll_into_view(state.tree_scroll, cursor_idx, visible.size(), viewport_rows);
}

}  // namespace toe::ui
