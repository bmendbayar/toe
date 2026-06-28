#pragma once

#include <string>
#include <variant>

namespace toe::ui
{

// clang-format off
struct CmdAddSiblingBelow {};   // 'o' new task after cursor, enter Insert
struct CmdAddSiblingAbove {};   // 'O' new task before cursor, enter Insert
struct CmdIndent {};            // Tab
struct CmdOutdent {};           // Shift+Tab
struct CmdDeleteTask {};        // 'dd'
struct CmdToggleDone {};        // 'x' toggle Done <-> Todo
struct CmdCyclePriority {};     // 'p'
struct CmdCycleStatus {};       // 's'
struct CmdCommitEdit { std::string text; };
struct CmdBeginEditDueDate {};                // 'D' — enter Insert for due date field
struct CmdCommitDueDate { std::string text; };  // Return in Insert/DueDate; empty = clear
struct CmdQuit {};
struct CmdForceQuit {};   // ':q!' — discard unsaved changes and quit
struct CmdSave {};
struct CmdSaveAndQuit {};
// clang-format on

using Command = std::variant<
    CmdAddSiblingBelow,
    CmdAddSiblingAbove,
    CmdIndent,
    CmdOutdent,
    CmdDeleteTask,
    CmdToggleDone,
    CmdCyclePriority,
    CmdCycleStatus,
    CmdCommitEdit,
    CmdBeginEditDueDate,
    CmdCommitDueDate,
    CmdQuit,
    CmdForceQuit,
    CmdSave,
    CmdSaveAndQuit>;

}  // namespace toe::ui
