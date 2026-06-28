#pragma once

#include <expected>
#include <ftxui/component/event.hpp>
#include <optional>
#include <vector>

#include "core/error.hpp"
#include "core/node_pool.hpp"
#include "core/task_id.hpp"
#include "ui/commands.hpp"
#include "ui/view_state.hpp"

namespace toe::ui
{

// DFS pre-order task list, skipping children of folded nodes.
[[nodiscard]] std::vector<core::TaskId>
compute_visible(const core::NodePool &pool, const FoldSet &folded);

struct EventResult
{
    bool consumed;
    std::optional<Command> command;
};

// Translate one key event into a result. Pure view-state transitions (mode
// switch, cursor move, fold) are applied directly to `state`; Core mutations
// are returned as a Command for the caller to apply via apply_command.
// Unconsumed events have consumed=false and leave `state` unchanged.
[[nodiscard]] EventResult process_event(
    ViewState &state, const std::vector<core::TaskId> &visible, const ftxui::Event &event
);

// Execute a command against the node pool, updating `state` as a side-effect
// (e.g. cursor after insert, mode after entering Insert).
// CmdQuit / CmdSave / CmdSaveAndQuit are no-ops here; the app layer handles them.
[[nodiscard]] std::expected<void, core::Error> apply_command(
    const Command &cmd, core::NodePool &pool, ViewState &state, const std::vector<core::TaskId> &visible
);

}  // namespace toe::ui
