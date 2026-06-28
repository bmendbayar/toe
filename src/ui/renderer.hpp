#pragma once

#include <ftxui/dom/elements.hpp>
#include <vector>

#include "core/node_pool.hpp"
#include "core/task_id.hpp"
#include "ui/view_state.hpp"

namespace toe::ui
{

// Produce the main task-list Element. Pure: reads Core + ViewState, no
// side-effects. Render is driven by the pre-computed visible list so the
// caller controls fold filtering.
[[nodiscard]] ftxui::Element render(
    const core::NodePool &pool, const ViewState &state, const std::vector<core::TaskId> &visible
);

// Status bar: mode indicator, command buffer, or edit prompt.
[[nodiscard]] ftxui::Element render_status_bar(const ViewState &state);

}  // namespace toe::ui
