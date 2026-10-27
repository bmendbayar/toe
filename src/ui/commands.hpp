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
struct CmdBeginEditTitle { bool cursor_at_end = true; };  // 'i'/'a' (end) or 'I' (start) — edit existing title
struct CmdCommitTitle { std::string text; };
struct CmdBeginDatePicker {};                 // 'D' — open calendar date picker
struct CmdCommitDueDate { std::string text; };  // Return in Insert/DueDate; empty = clear
struct CmdQuit {};
struct CmdForceQuit {};   // ':q!' — discard unsaved changes and quit
struct CmdSave {};
struct CmdSaveAndQuit {};
struct CmdExport { std::string target; };   // ';export <target>' — copy cursor subtree to another file
struct CmdEditDescription {};               // 'e' — edit cursor's description in $EDITOR (app-level)
struct CmdOpen { std::string target; bool force = false; };  // ';open[!] [target]' — switch files; empty target = picker (app-level)
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
    CmdBeginEditTitle,
    CmdCommitTitle,
    CmdBeginDatePicker,
    CmdCommitDueDate,
    CmdQuit,
    CmdForceQuit,
    CmdSave,
    CmdSaveAndQuit,
    CmdExport,
    CmdEditDescription,
    CmdOpen>;

}  // namespace toe::ui
