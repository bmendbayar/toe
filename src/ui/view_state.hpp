#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "core/task_id.hpp"

namespace toe::ui
{

enum class Mode : std::uint8_t
{
    Normal,
    Insert,
    Command,
};

struct TaskIdHash
{
    [[nodiscard]] std::size_t operator()(core::TaskId id) const noexcept
    {
        return std::hash<std::uint64_t>{}(
            (std::uint64_t{id.index} << 32) | id.generation
        );
    }
};

using FoldSet = std::unordered_set<core::TaskId, TaskIdHash>;

struct ViewState
{
    Mode mode = Mode::Normal;
    core::TaskId cursor;      // focused task; invalid() = no focus
    FoldSet folded;           // roots of collapsed subtrees
    std::string edit_buffer;  // accumulator in Insert mode
    std::string cmd_buffer;   // accumulator in Command mode
    bool pending_d = false;   // 'd' pressed once; second 'd' fires CmdDeleteTask
};

}  // namespace toe::ui
