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
struct CmdQuit {};
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
    CmdQuit,
    CmdSave,
    CmdSaveAndQuit>;

}  // namespace toe::ui
