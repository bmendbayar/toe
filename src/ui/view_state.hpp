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
    DatePicker,
};

enum class ViewMode : std::uint8_t
{
    Tree,
    Timeline,
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

enum class EditField : std::uint8_t
{
    Description,
    DueDate,
};

struct DatePickerState
{
    std::int64_t view_epoch = 0;  // selected day in the calendar grid
};

struct ViewState
{
    Mode mode = Mode::Normal;
    ViewMode view_mode = ViewMode::Tree;
    EditField edit_field = EditField::Description;  // which field 'i'/D edits
    core::TaskId cursor;                            // focused task; invalid() = no focus
    FoldSet folded;                                 // roots of collapsed subtrees
    std::string edit_buffer;                        // accumulator in Insert mode
    std::string cmd_buffer;                         // accumulator in Command mode
    bool pending_d = false;                         // 'd' pressed once; second 'd' fires CmdDeleteTask
    DatePickerState picker;
    std::int64_t timeline_start_epoch = 0;  // first visible day in Timeline view
};

}  // namespace toe::ui
