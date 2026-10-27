#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "core/error.hpp"
#include "core/fields.hpp"
#include "core/node_pool.hpp"
#include "core/task_id.hpp"
#include "ui/commands.hpp"
#include "ui/date_util.hpp"
#include "ui/editor_text.hpp"
#include "ui/key_handler.hpp"
#include "ui/picker.hpp"
#include "ui/renderer.hpp"
#include "ui/scroll.hpp"
#include "ui/view_state.hpp"

namespace toe::ui
{

using core::NodePool;
using core::TaskId;

// --- compute_visible ----------------------------------------------------------

TEST(ComputeVisible, EmptyPoolReturnsEmpty)
{
    NodePool pool;
    FoldSet folded;
    EXPECT_TRUE(compute_visible(pool, folded).empty());
}

TEST(ComputeVisible, FlatForestReturnAllRoots)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    const TaskId c = pool.add_root();
    FoldSet folded;
    const auto vis = compute_visible(pool, folded);
    ASSERT_EQ(vis.size(), 3u);
    EXPECT_EQ(vis[0], a);
    EXPECT_EQ(vis[1], b);
    EXPECT_EQ(vis[2], c);
}

TEST(ComputeVisible, FoldedNodeHidesChildren)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId a1 = pool.add_child(a).value();
    const TaskId b = pool.add_root();

    FoldSet folded;
    folded.insert(a);

    const auto vis = compute_visible(pool, folded);
    // a is folded: a1 must not appear; b still appears
    ASSERT_EQ(vis.size(), 2u);
    EXPECT_EQ(vis[0], a);
    EXPECT_EQ(vis[1], b);
    (void)a1;
}

TEST(ComputeVisible, UnfoldedNodeShowsDescendants)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId a1 = pool.add_child(a).value();
    const TaskId a2 = pool.add_child(a).value();

    FoldSet folded;  // nothing folded

    const auto vis = compute_visible(pool, folded);
    ASSERT_EQ(vis.size(), 3u);
    EXPECT_EQ(vis[0], a);
    EXPECT_EQ(vis[1], a1);
    EXPECT_EQ(vis[2], a2);
}

// --- Mode transitions ----------------------------------------------------------

TEST(ModeTransition, NormalI_EmitsCmdBeginEditTitle_AtEnd)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);
    state.cursor = root;

    const auto result = process_event(state, vis, ftxui::Event::i);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    const auto *cmd = std::get_if<CmdBeginEditTitle>(&*result.command);
    ASSERT_NE(cmd, nullptr);
    EXPECT_TRUE(cmd->cursor_at_end);
    EXPECT_EQ(state.mode, Mode::NORMAL);  // mode flips when the command is applied
}

TEST(ModeTransition, NormalA_EmitsCmdBeginEditTitle_AtEnd)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::a);

    ASSERT_TRUE(result.command.has_value());
    const auto *cmd = std::get_if<CmdBeginEditTitle>(&*result.command);
    ASSERT_NE(cmd, nullptr);
    EXPECT_TRUE(cmd->cursor_at_end);
}

TEST(ModeTransition, NormalShiftI_EmitsCmdBeginEditTitle_AtStart)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::I);

    ASSERT_TRUE(result.command.has_value());
    const auto *cmd = std::get_if<CmdBeginEditTitle>(&*result.command);
    ASSERT_NE(cmd, nullptr);
    EXPECT_FALSE(cmd->cursor_at_end);
}

TEST(ModeTransition, InsertEsc_ReturnsNormal)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::INSERT;
    state.edit_buffer = "hello";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Escape);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::NORMAL);
    EXPECT_TRUE(state.edit_buffer.empty());
}

TEST(ModeTransition, CommandEsc_ReturnsNormal)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "wq";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Escape);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::NORMAL);
    EXPECT_TRUE(state.cmd_buffer.empty());
}

// --- Normal mode keymap dispatch ----------------------------------------------------------

TEST(NormalMode, J_MovesCursorDown)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::j);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.cursor, b);
}

TEST(NormalMode, K_MovesCursorUp)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = b;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::k);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.cursor, a);
}

TEST(NormalMode, J_ClampsAtLastRow)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::j);

    EXPECT_EQ(state.cursor, a);  // already at last; no change
}

TEST(NormalMode, K_ClampsAtFirstRow)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::k);

    EXPECT_EQ(state.cursor, a);  // already at first; no change
}

TEST(NormalMode, g_JumpsToFirst)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = b;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::g);

    EXPECT_TRUE(result.consumed);
    EXPECT_EQ(state.cursor, a);
}

TEST(NormalMode, G_JumpsToLast)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::G);

    EXPECT_TRUE(result.consumed);
    EXPECT_EQ(state.cursor, b);
}

TEST(NormalMode, H_FoldsCurrentTask)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::h);

    EXPECT_TRUE(state.folded.contains(a));
}

TEST(NormalMode, L_UnfoldsCurrentTask)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    state.folded.insert(a);
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::l);

    EXPECT_FALSE(state.folded.contains(a));
}

TEST(NormalMode, o_EmitsCmdAddSiblingBelow)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);
    state.cursor = root;

    const auto result = process_event(state, vis, ftxui::Event::o);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdAddSiblingBelow>(*result.command));
}

TEST(NormalMode, O_EmitsCmdAddSiblingAbove)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);
    state.cursor = root;

    const auto result = process_event(state, vis, ftxui::Event::O);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdAddSiblingAbove>(*result.command));
}

TEST(NormalMode, Tab_EmitsCmdIndent)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Tab);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdIndent>(*result.command));
}

TEST(NormalMode, TabReverse_EmitsCmdOutdent)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::TabReverse);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdOutdent>(*result.command));
}

TEST(NormalMode, x_EmitsCmdToggleDone)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::x);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdToggleDone>(*result.command));
}

TEST(NormalMode, dd_EmitsCmdDeleteTask)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    // First 'd': arms the sequence, no command yet
    const auto first = process_event(state, vis, ftxui::Event::d);
    EXPECT_TRUE(first.consumed);
    EXPECT_FALSE(first.command.has_value());
    EXPECT_TRUE(state.pending_d);

    // Second 'd': fires delete
    const auto second = process_event(state, vis, ftxui::Event::d);
    EXPECT_TRUE(second.consumed);
    ASSERT_TRUE(second.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdDeleteTask>(*second.command));
    EXPECT_FALSE(state.pending_d);
}

TEST(NormalMode, Esc_CancelsPendingD)
{
    NodePool pool;
    ViewState state;
    state.pending_d = true;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Escape);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(state.pending_d);
    EXPECT_FALSE(result.command.has_value());
}

TEST(NormalMode, p_EmitsCmdCyclePriority)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::p);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdCyclePriority>(*result.command));
}

TEST(NormalMode, s_EmitsCmdCycleStatus)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::s);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdCycleStatus>(*result.command));
}

TEST(NormalMode, q_EmitsCmdQuit)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::q);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdQuit>(*result.command));
}

TEST(NormalMode, D_EmitsCmdBeginDatePicker)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::D);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdBeginDatePicker>(*result.command));
}

TEST(NormalMode, UnknownKey_IsNotConsumed)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::F1);

    EXPECT_FALSE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
}

// --- Insert mode keymap ----------------------------------------------------------

TEST(InsertMode, PrintableChar_AppendsToBuffer)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::INSERT;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character('h'));
    (void)process_event(state, vis, ftxui::Event::Character('i'));

    EXPECT_EQ(state.edit_buffer, "hi");
    EXPECT_EQ(state.mode, Mode::INSERT);
}

TEST(InsertMode, Backspace_PopsLastChar)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::INSERT;
    state.edit_buffer = "abc";
    state.edit_cursor = 3;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Backspace);

    EXPECT_EQ(state.edit_buffer, "ab");
    EXPECT_EQ(state.edit_cursor, 2u);
}

TEST(InsertMode, Return_EmitsCmdCommitTitle_AndReturnToNormal)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::INSERT;
    state.edit_buffer = "Buy milk";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    ASSERT_TRUE(std::holds_alternative<CmdCommitTitle>(*result.command));
    EXPECT_EQ(std::get<CmdCommitTitle>(*result.command).text, "Buy milk");
    EXPECT_EQ(state.mode, Mode::NORMAL);
    EXPECT_TRUE(state.edit_buffer.empty());
}

TEST(InsertMode, Return_InDateField_EmitsCmdCommitDueDate)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::INSERT;
    state.edit_field = EditField::DUE_DATE;
    state.edit_buffer = "2025-07-04";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    ASSERT_TRUE(std::holds_alternative<CmdCommitDueDate>(*result.command));
    EXPECT_EQ(std::get<CmdCommitDueDate>(*result.command).text, "2025-07-04");
    EXPECT_EQ(state.mode, Mode::NORMAL);
    EXPECT_EQ(state.edit_field, EditField::TITLE);
    EXPECT_TRUE(state.edit_buffer.empty());
}

// --- Command mode keymap ----------------------------------------------------------

TEST(CommandMode, CharAccumulates)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character('w'));

    EXPECT_EQ(state.cmd_buffer, "w");
}

TEST(CommandMode, Return_W_EmitsCmdSave)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "w";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdSave>(*result.command));
    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(CommandMode, Return_Q_EmitsCmdQuit)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "q";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdQuit>(*result.command));
}

TEST(CommandMode, Return_Export_EmitsCmdExportWithTarget)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "export  archive ";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    ASSERT_TRUE(result.command.has_value());
    const auto *exp = std::get_if<CmdExport>(&*result.command);
    ASSERT_NE(exp, nullptr);
    EXPECT_EQ(exp->target, "archive");
}

TEST(CommandMode, Return_BareExport_EmitsCmdExportWithEmptyTarget)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "export";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    ASSERT_TRUE(result.command.has_value());
    const auto *exp = std::get_if<CmdExport>(&*result.command);
    ASSERT_NE(exp, nullptr);
    EXPECT_TRUE(exp->target.empty());
}

TEST(CommandMode, Return_ExportPrefixedWord_EmitsNothing)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "exporter";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_FALSE(result.command.has_value());
}

namespace
{

std::optional<CmdOpen> open_from_command_line(const std::string &line)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = line;
    const auto vis = compute_visible(pool, state.folded);
    const auto result = process_event(state, vis, ftxui::Event::Return);
    if (!result.command)
    {
        return std::nullopt;
    }
    const auto *open = std::get_if<CmdOpen>(&*result.command);
    return open != nullptr ? std::optional<CmdOpen>{*open} : std::nullopt;
}

}  // namespace

TEST(CommandMode, Return_BareOpen_EmitsCmdOpenWithEmptyTarget)
{
    const auto open = open_from_command_line("open");
    ASSERT_TRUE(open.has_value());
    EXPECT_TRUE(open->target.empty());
    EXPECT_FALSE(open->force);
}

TEST(CommandMode, Return_OpenBang_EmitsForcedCmdOpen)
{
    const auto open = open_from_command_line("open!");
    ASSERT_TRUE(open.has_value());
    EXPECT_TRUE(open->target.empty());
    EXPECT_TRUE(open->force);
}

TEST(CommandMode, Return_OpenWithTarget_TrimsTarget)
{
    const auto open = open_from_command_line("open  dsa ");
    ASSERT_TRUE(open.has_value());
    EXPECT_EQ(open->target, "dsa");
    EXPECT_FALSE(open->force);
}

TEST(CommandMode, Return_OpenBangWithTarget_EmitsForcedCmdOpen)
{
    const auto open = open_from_command_line("open! ~/notes.toe");
    ASSERT_TRUE(open.has_value());
    EXPECT_EQ(open->target, "~/notes.toe");
    EXPECT_TRUE(open->force);
}

TEST(CommandMode, Return_OpenPrefixedWord_EmitsNothing)
{
    EXPECT_FALSE(open_from_command_line("opener").has_value());
}

// --- file picker helpers ------------------------------------------------------

TEST(Picker, DisplayName_BeneathCwd_IsRelative)
{
    EXPECT_EQ(display_name("/work/proj/a/x.toe", "/work/proj", "/home/u"), "a/x.toe");
}

TEST(Picker, DisplayName_BeneathHome_UsesTilde)
{
    EXPECT_EQ(display_name("/home/u/.config/toe/t.bin", "/work/proj", "/home/u"), "~/.config/toe/t.bin");
}

TEST(Picker, DisplayName_Elsewhere_IsAbsolute)
{
    EXPECT_EQ(display_name("/srv/x.toe", "/work/proj", "/home/u"), "/srv/x.toe");
}

TEST(Picker, DisplayName_CwdWinsOverHome)
{
    EXPECT_EQ(display_name("/home/u/proj/x.toe", "/home/u/proj", "/home/u"), "x.toe");
}

TEST(Picker, Parse_ExitZero_ReturnsSelectedPath)
{
    const std::map<std::string, std::filesystem::path> known{{"a.toe", "/w/a.toe"}, {"b.toe", "/w/b.toe"}};
    const auto r = parse_picker_output("b\nb.toe\n", 0, known, "/cfg");
    const auto *pick = std::get_if<PickPath>(&r);
    ASSERT_NE(pick, nullptr);
    EXPECT_EQ(pick->path, std::filesystem::path{"/w/b.toe"});
}

TEST(Picker, Parse_ExitZeroUnknownSelection_Cancels)
{
    const std::map<std::string, std::filesystem::path> known{{"a.toe", "/w/a.toe"}};
    EXPECT_TRUE(std::holds_alternative<PickCancelled>(parse_picker_output("x\nzzz\n", 0, known, "/cfg")));
}

TEST(Picker, Parse_NoMatchWithQuery_CreatesNewFileInConfigDir)
{
    const auto r = parse_picker_output("scratch\n", 1, {}, "/cfg/toe");
    const auto *pick = std::get_if<PickPath>(&r);
    ASSERT_NE(pick, nullptr);
    EXPECT_EQ(pick->path, std::filesystem::path{"/cfg/toe/scratch.toe"});
}

TEST(Picker, Parse_NoMatchWithPathQuery_UsesPathAsGiven)
{
    const auto r = parse_picker_output("sub/new.toe\n", 1, {}, "/cfg/toe");
    const auto *pick = std::get_if<PickPath>(&r);
    ASSERT_NE(pick, nullptr);
    EXPECT_EQ(pick->path, std::filesystem::path{"sub/new.toe"});
}

TEST(Picker, Parse_NoMatchEmptyQuery_Cancels)
{
    EXPECT_TRUE(std::holds_alternative<PickCancelled>(parse_picker_output("\n", 1, {}, "/cfg")));
}

TEST(Picker, Parse_Interrupted_Cancels)
{
    const std::map<std::string, std::filesystem::path> known{{"a.toe", "/w/a.toe"}};
    EXPECT_TRUE(std::holds_alternative<PickCancelled>(parse_picker_output("a\na.toe\n", 130, known, "/cfg")));
}

TEST(CommandMode, Return_WQ_EmitsCmdSaveAndQuit)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "wq";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdSaveAndQuit>(*result.command));
}

TEST(CommandMode, Return_X_EmitsCmdSaveAndQuit)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "x";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdSaveAndQuit>(*result.command));
}

TEST(CommandMode, Return_UnknownCmd_NoCommand)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "foo";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(CommandMode, Return_QBang_EmitsCmdForceQuit)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "q!";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdForceQuit>(*result.command));
    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(CommandMode, Backspace_PopsLastChar)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "wq";
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Backspace);

    EXPECT_EQ(state.cmd_buffer, "w");
}

// --- apply_command ----------------------------------------------------------

TEST(ApplyCommand, AddSiblingBelow_EmptyPool_CreatesRoot)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto res = apply_command(Command{CmdAddSiblingBelow{}}, pool, state, vis);

    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(pool.live_count(), 1u);
    EXPECT_TRUE(state.cursor.is_valid());
    EXPECT_EQ(state.mode, Mode::INSERT);
}

TEST(ApplyCommand, AddSiblingBelow_CreatesSiblingAfterCursor)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    const auto res = apply_command(Command{CmdAddSiblingBelow{}}, pool, state, vis);

    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(pool.live_count(), 3u);
    EXPECT_EQ(state.mode, Mode::INSERT);
    // New task must be immediately after a in the root chain
    const TaskId new_task = state.cursor;
    EXPECT_EQ(pool.next_sibling(a).value(), new_task);
    EXPECT_EQ(pool.next_sibling(new_task).value(), b);
}

TEST(ApplyCommand, AddSiblingAbove_CreatesBeforeCursor)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = b;
    const auto vis = compute_visible(pool, state.folded);

    const auto res = apply_command(Command{CmdAddSiblingAbove{}}, pool, state, vis);

    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(pool.live_count(), 3u);
    EXPECT_EQ(state.mode, Mode::INSERT);
    // New task must be between a and b
    const TaskId new_task = state.cursor;
    EXPECT_EQ(pool.next_sibling(a).value(), new_task);
    EXPECT_EQ(pool.next_sibling(new_task).value(), b);
}

TEST(ApplyCommand, DeleteTask_RemovesNodeAndAdvancesCursor)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = a;
    auto vis = compute_visible(pool, state.folded);

    const auto res = apply_command(Command{CmdDeleteTask{}}, pool, state, vis);

    EXPECT_TRUE(res.has_value());
    EXPECT_FALSE(pool.is_valid(a));
    EXPECT_EQ(state.cursor, b);  // advanced to next
}

TEST(ApplyCommand, DeleteLastTask_CursorBecomesInvalid)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    const auto res = apply_command(Command{CmdDeleteTask{}}, pool, state, vis);

    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(pool.live_count(), 0u);
    EXPECT_FALSE(state.cursor.is_valid());
}

TEST(ApplyCommand, DeleteSubtree_FallbackCursorSkipsDescendants)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId a1 = pool.add_child(a).value();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);
    // visible = [a, a1, b]; next after a is a1 (a descendant, will be deleted)
    // so fallback must resolve to b

    const auto res = apply_command(Command{CmdDeleteTask{}}, pool, state, vis);

    EXPECT_TRUE(res.has_value());
    EXPECT_FALSE(pool.is_valid(a));
    EXPECT_FALSE(pool.is_valid(a1));
    EXPECT_EQ(state.cursor, b);
}

TEST(ApplyCommand, ToggleDone_SetsDone)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdToggleDone{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.get_status(a).value(), core::Status::DONE);
}

TEST(ApplyCommand, ToggleDone_TogglesBackToTodo)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ASSERT_TRUE(pool.set_status(a, core::Status::DONE).has_value());
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdToggleDone{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.get_status(a).value(), core::Status::TODO);
}

TEST(ApplyCommand, CyclePriority_AdvancesPriority)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdCyclePriority{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.get_priority(a).value(), core::Priority::MEDIUM);
}

TEST(ApplyCommand, CycleStatus_AdvancesStatus)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdCycleStatus{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.get_status(a).value(), core::Status::IN_PROGRESS);
}

TEST(ApplyCommand, CommitEdit_SetsTitle)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(
                    Command{CmdCommitTitle{.text = "Buy milk"}}, pool, state, vis
    )
                    .has_value());

    EXPECT_EQ(pool.get_title(a).value(), "Buy milk");
}

TEST(ApplyCommand, CommitEdit_EmptyText_DoesNothing)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ASSERT_TRUE(pool.set_title(a, "existing").has_value());
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(
        apply_command(Command{CmdCommitTitle{.text = ""}}, pool, state, vis)
            .has_value()
    );

    // title unchanged
    EXPECT_EQ(pool.get_title(a).value(), "existing");
}

TEST(ApplyCommand, Indent_ReparentsUnderPrevSibling)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ViewState state;
    state.cursor = b;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdIndent{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.parent(b).value(), a);
}

TEST(ApplyCommand, Outdent_LiftsToGrandparent)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId a1 = pool.add_child(a).value();
    ViewState state;
    state.cursor = a1;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdOutdent{}}, pool, state, vis).has_value());

    EXPECT_FALSE(pool.parent(a1).value().is_valid());  // now a root
}

TEST(ApplyCommand, Quit_IsNoOpOnPool)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    EXPECT_TRUE(apply_command(Command{CmdQuit{}}, pool, state, vis).has_value());
    EXPECT_EQ(pool.live_count(), 0u);
}

TEST(ApplyCommand, BeginDatePicker_EntersDatePickerMode)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    // now_seconds=0: no date set, so picker seeds to today_epoch(0) = 0
    ASSERT_TRUE(apply_command(Command{CmdBeginDatePicker{}}, pool, state, vis).has_value());

    EXPECT_EQ(state.mode, Mode::DATE_PICKER);
    EXPECT_EQ(state.picker.view_epoch, today_epoch(0));
}

TEST(ApplyCommand, BeginDatePicker_SeedsPickerFromExistingDate)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    using namespace std::chrono;
    const auto epoch =
        sys_days{year{2025} / July / 4}.time_since_epoch().count() * std::int64_t{86400};
    ASSERT_TRUE(pool.set_due_date(a, epoch).has_value());
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdBeginDatePicker{}}, pool, state, vis).has_value());

    EXPECT_EQ(state.mode, Mode::DATE_PICKER);
    EXPECT_EQ(state.picker.view_epoch, epoch);
}

TEST(ApplyCommand, CommitDueDate_ParsesAndStoresDate)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    using namespace std::chrono;
    const auto expected =
        sys_days{year{2025} / July / 4}.time_since_epoch().count() * std::int64_t{86400};

    ASSERT_TRUE(
        apply_command(Command{CmdCommitDueDate{.text = "2025-07-04"}}, pool, state, vis).has_value()
    );

    const auto date = pool.get_due_date(a);
    ASSERT_TRUE(date.has_value());
    ASSERT_TRUE(date->has_value());
    EXPECT_EQ(**date, expected);
}

TEST(ApplyCommand, CommitDueDate_EmptyText_ClearsDate)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ASSERT_TRUE(pool.set_due_date(a, std::int64_t{0}).has_value());
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(
        apply_command(Command{CmdCommitDueDate{.text = ""}}, pool, state, vis).has_value()
    );

    const auto date = pool.get_due_date(a);
    ASSERT_TRUE(date.has_value());
    EXPECT_FALSE(date->has_value());
}

TEST(ApplyCommand, CommitDueDate_InvalidText_Noop)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ASSERT_TRUE(pool.set_due_date(a, std::int64_t{86400}).has_value());
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(
        apply_command(Command{CmdCommitDueDate{.text = "not-a-date"}}, pool, state, vis).has_value()
    );

    // Date unchanged
    const auto date = pool.get_due_date(a);
    ASSERT_TRUE(date.has_value());
    ASSERT_TRUE(date->has_value());
    EXPECT_EQ(**date, std::int64_t{86400});
}

// --- DatePicker mode keymap ----------------------------------------------------------

// Helper: epoch for a specific calendar date.
static std::int64_t make_epoch(int y, unsigned m, unsigned d)
{
    using namespace std::chrono;
    return ymd_to_epoch(year_month_day{year{y}, month{m}, day{d}});
}

TEST(DatePickerMode, H_MovesBackOneDay)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 15);
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::h);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 7, 14));
}

TEST(DatePickerMode, L_MovesForwardOneDay)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 14);
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::l);

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 7, 15));
}

TEST(DatePickerMode, J_MovesForwardOneWeek)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 1);
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::j);

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 7, 8));
}

TEST(DatePickerMode, K_MovesBackOneWeek)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 8);
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::k);

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 7, 1));
}

TEST(DatePickerMode, BracketLeft_MovesBackOneMonth)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 15);
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character('['));

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 6, 15));
}

TEST(DatePickerMode, BracketRight_MovesForwardOneMonth)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 6, 15);
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character(']'));

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 7, 15));
}

TEST(DatePickerMode, T_JumpsToToday)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2020, 1, 1);
    const auto vis = compute_visible(pool, state.folded);

    // now_seconds points to a specific instant in July 2026
    const std::int64_t now = make_epoch(2026, 7, 10) + 3600;  // July 10 + 1 hour offset
    const auto result = process_event(state, vis, ftxui::Event::t, now);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.picker.view_epoch, today_epoch(now));  // midnight of July 10
}

TEST(DatePickerMode, Enter_EmitsCmdCommitDueDateWithSelectedDate)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 15);
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    ASSERT_TRUE(std::holds_alternative<CmdCommitDueDate>(*result.command));
    EXPECT_EQ(std::get<CmdCommitDueDate>(*result.command).text, "2026-07-15");
    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(DatePickerMode, D_EmitsCmdCommitDueDateEmpty)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 15);
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::d);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    ASSERT_TRUE(std::holds_alternative<CmdCommitDueDate>(*result.command));
    EXPECT_TRUE(std::get<CmdCommitDueDate>(*result.command).text.empty());
    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(DatePickerMode, Esc_ReturnsToNormal_NoCmdNoDateChange)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 15);
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Escape);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::NORMAL);
    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 7, 15));  // unchanged
}

TEST(DatePickerMode, I_HandoffToInsert_PreFillsBuffer)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 7, 15);
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::i);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::INSERT);
    EXPECT_EQ(state.edit_field, EditField::DUE_DATE);
    EXPECT_EQ(state.edit_buffer, "2026-07-15");
}

TEST(DatePickerMode, H_WrapsAcrossMonthBoundary)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 8, 1);  // Aug 1
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::h);  // -1 day

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 7, 31));  // Jul 31
}

TEST(DatePickerMode, H_WrapsAcrossYearBoundary)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 1, 1);  // Jan 1
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::h);  // -1 day

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2025, 12, 31));  // Dec 31 prev year
}

TEST(DatePickerMode, L_SelectsLeapDay)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2024, 2, 28);  // 2024 is a leap year
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::l);  // +1 day

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2024, 2, 29));
}

TEST(DatePickerMode, BracketRight_ClampsEndOfMonth)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 1, 31);  // Jan 31
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character(']'));  // +1 month

    // Feb 2026 has 28 days (not a leap year), so Jan 31 + 1 month clamps to Feb 28
    EXPECT_EQ(state.picker.view_epoch, make_epoch(2026, 2, 28));
}

TEST(DatePickerMode, BracketLeft_WrapsAcrossYear)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = make_epoch(2026, 1, 15);  // Jan 15
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character('['));  // -1 month

    EXPECT_EQ(state.picker.view_epoch, make_epoch(2025, 12, 15));  // Dec 15 prev year
}

// --- NormalMode: timeline view toggle (T key) --------------------------------

TEST(NormalMode, T_SwitchesToTimeline)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);
    constexpr std::int64_t kNow = 1'000'000;  // arbitrary "now"

    const auto result = process_event(state, vis, ftxui::Event::T, kNow);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.view_mode, ViewMode::TIMELINE);
    EXPECT_EQ(state.timeline_start_epoch, today_epoch(kNow));
}

TEST(NormalMode, T_TogglesBackToTree)
{
    NodePool pool;
    ViewState state;
    state.view_mode = ViewMode::TIMELINE;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::T, 0);

    EXPECT_TRUE(result.consumed);
    EXPECT_EQ(state.view_mode, ViewMode::TREE);
}

// --- NormalMode: timeline scroll ([/] keys) ----------------------------------

TEST(NormalMode, BracketLeft_ScrollsTimelineBack)
{
    NodePool pool;
    ViewState state;
    state.view_mode = ViewMode::TIMELINE;
    state.timeline_start_epoch = 7 * 86400;  // day 7
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Character('['));

    EXPECT_TRUE(result.consumed);
    EXPECT_EQ(state.timeline_start_epoch, 0);  // day 7 - 7 = day 0
}

TEST(NormalMode, BracketRight_ScrollsTimelineForward)
{
    NodePool pool;
    ViewState state;
    state.view_mode = ViewMode::TIMELINE;
    state.timeline_start_epoch = 0;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Character(']'));

    EXPECT_TRUE(result.consumed);
    EXPECT_EQ(state.timeline_start_epoch, 7 * 86400);
}

TEST(NormalMode, BracketLeft_InTreeMode_IsNotConsumed)
{
    NodePool pool;
    ViewState state;
    // view_mode defaults to Tree
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Character('['));

    EXPECT_FALSE(result.consumed);
}

TEST(NormalMode, BracketRight_InTreeMode_IsNotConsumed)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Character(']'));

    EXPECT_FALSE(result.consumed);
}

// --- CommandMode: :timeline and :tree ----------------------------------------

TEST(CommandMode, Return_Timeline_SwitchesToTimelineView)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.cmd_buffer = "timeline";
    constexpr std::int64_t kNow = 500'000;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return, kNow);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::NORMAL);
    EXPECT_EQ(state.view_mode, ViewMode::TIMELINE);
    EXPECT_EQ(state.timeline_start_epoch, today_epoch(kNow));
}

TEST(CommandMode, Return_Tree_SwitchesToTreeView)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::COMMAND;
    state.view_mode = ViewMode::TIMELINE;
    state.cmd_buffer = "tree";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::NORMAL);
    EXPECT_EQ(state.view_mode, ViewMode::TREE);
}

// --- scroll_into_view -----------------------------------------------------------

TEST(ScrollIntoView, FitsOnScreen_ReturnsZero)
{
    EXPECT_EQ(scroll_into_view(4, 2, 10, 10), 0u);
    EXPECT_EQ(scroll_into_view(0, 0, 3, 10), 0u);
}

TEST(ScrollIntoView, CursorInMiddle_NoScroll)
{
    EXPECT_EQ(scroll_into_view(5, 10, 100, 20), 5u);
}

TEST(ScrollIntoView, CursorNearBottom_ScrollsByOne)
{
    // viewport 20, top 0: rows 0..19; cursor at 16 is exactly SCROLL_OFF from the edge.
    EXPECT_EQ(scroll_into_view(0, 16, 100, 20), 0u);
    EXPECT_EQ(scroll_into_view(0, 17, 100, 20), 1u);
}

TEST(ScrollIntoView, CursorNearTop_ScrollsUp)
{
    EXPECT_EQ(scroll_into_view(10, 13, 100, 20), 10u);
    EXPECT_EQ(scroll_into_view(10, 12, 100, 20), 9u);
}

TEST(ScrollIntoView, JumpToLast_ShowsLastPage)
{
    EXPECT_EQ(scroll_into_view(0, 99, 100, 20), 80u);
}

TEST(ScrollIntoView, JumpToFirst_ReturnsZero)
{
    EXPECT_EQ(scroll_into_view(80, 0, 100, 20), 0u);
}

TEST(ScrollIntoView, ListShrank_ClampsTop)
{
    EXPECT_EQ(scroll_into_view(80, 25, 30, 20), 10u);
}

TEST(ScrollIntoView, TinyViewport_ScrolloffShrinks)
{
    for (std::size_t vp : {1u, 2u, 3u})
    {
        for (std::size_t cur = 0; cur < 10; ++cur)
        {
            const std::size_t top = scroll_into_view(0, cur, 10, vp);
            EXPECT_LE(top, cur);
            EXPECT_LT(cur, top + vp);
        }
    }
}

TEST(ScrollIntoView, CursorAbsent_OnlyClamps)
{
    EXPECT_EQ(scroll_into_view(50, 100, 100, 20), 50u);
    EXPECT_EQ(scroll_into_view(95, 100, 100, 20), 80u);
}

// --- render scrolling -------------------------------------------------------------

TEST(Render, TreeStartsAtScrollOffset)
{
    NodePool pool;
    TaskId id = pool.add_root();
    ASSERT_TRUE(pool.set_title(id, "t0"));
    for (int i = 1; i < 10; ++i)
    {
        auto next = pool.add_sibling_after(id);
        ASSERT_TRUE(next.has_value());
        id = *next;
        ASSERT_TRUE(pool.set_title(id, "t" + std::to_string(i)));
    }

    ViewState state;
    state.tree_scroll = 5;
    const auto vis = compute_visible(pool, state.folded);

    ftxui::Screen screen(40, 3);
    ftxui::Render(screen, render(pool, state, vis, 0));
    const std::string out = screen.ToString();

    EXPECT_NE(out.find("t5"), std::string::npos);
    EXPECT_EQ(out.find("t0"), std::string::npos);
}

// --- descriptions -------------------------------------------------------------

TEST(NormalMode, K_TogglesDetailsPane)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    auto result = process_event(state, vis, ftxui::Event::K);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_TRUE(state.show_details);

    result = process_event(state, vis, ftxui::Event::K);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(state.show_details);
}

TEST(NormalMode, e_WithCursor_EmitsCmdEditDescription)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::e);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdEditDescription>(*result.command));
    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(NormalMode, e_WithoutCursor_EmitsNothing)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::e);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
}

TEST(ApplyCommand, EditDescriptionIsNoOpInCore)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    ASSERT_TRUE(pool.set_description(state.cursor, "keep").has_value());
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdEditDescription{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.get_description(state.cursor).value(), std::string_view{"keep"});
    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(EditorText, StripsExactlyOneTrailingNewline)
{
    EXPECT_EQ(normalize_editor_text("a\nb\n"), "a\nb");
    EXPECT_EQ(normalize_editor_text("a\nb\n\n"), "a\nb\n");
    EXPECT_EQ(normalize_editor_text("a\r\n"), "a");
    EXPECT_EQ(normalize_editor_text("no newline"), "no newline");
    EXPECT_EQ(normalize_editor_text("\n"), "");
    EXPECT_EQ(normalize_editor_text(""), "");
}

TEST(EditorText, KeepsInnerBlankLines)
{
    EXPECT_EQ(normalize_editor_text("one\n\nthree\n"), "one\n\nthree");
}

TEST(Render, RowShowsMarkerOnlyWhenDescriptionPresent)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_sibling_after(a).value();
    ASSERT_TRUE(pool.set_title(a, "with notes"));
    ASSERT_TRUE(pool.set_title(b, "plain"));
    ASSERT_TRUE(pool.set_description(a, "details"));

    ViewState state;
    const auto vis = compute_visible(pool, state.folded);
    ftxui::Screen screen(40, 2);
    ftxui::Render(screen, render(pool, state, vis, 0));
    const std::string out = screen.ToString();

    const auto marker = out.find("✎");
    ASSERT_NE(marker, std::string::npos);
    EXPECT_EQ(out.find("✎", marker + 1), std::string::npos);  // only one row has it
}

TEST(Render, DetailsPaneShowsMultiLineDescriptionBesideList)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    ASSERT_TRUE(pool.set_title(state.cursor, "Ship v2"));
    ASSERT_TRUE(pool.set_description(state.cursor, "alpha line\n\nbeta line"));
    state.show_details = true;
    const auto vis = compute_visible(pool, state.folded);

    ftxui::Screen screen(100, 8);
    ftxui::Render(screen, render(pool, state, vis, 0, 100));
    const std::string out = screen.ToString();

    EXPECT_NE(out.find("alpha line"), std::string::npos);
    EXPECT_NE(out.find("beta line"), std::string::npos);
    EXPECT_NE(out.find("✎"), std::string::npos);
    EXPECT_EQ(details_pane_rows(state, 100), 0);
}

TEST(Render, DetailsPaneStacksBelowOnNarrowTerminal)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    ASSERT_TRUE(pool.set_title(state.cursor, "T"));
    ASSERT_TRUE(pool.set_description(state.cursor, "narrow notes"));
    state.show_details = true;
    const auto vis = compute_visible(pool, state.folded);

    ftxui::Screen screen(50, 14);
    ftxui::Render(screen, render(pool, state, vis, 0, 50));

    EXPECT_NE(screen.ToString().find("narrow notes"), std::string::npos);
    EXPECT_EQ(details_pane_rows(state, 50), DETAILS_PANE_ROWS);
}

TEST(Render, DetailsPaneHintsWhenDescriptionEmpty)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    state.show_details = true;
    const auto vis = compute_visible(pool, state.folded);

    ftxui::Screen screen(100, 6);
    ftxui::Render(screen, render(pool, state, vis, 0, 100));

    EXPECT_NE(screen.ToString().find("press e to add"), std::string::npos);
}

TEST(Render, DetailsPaneHiddenByDefault)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    ASSERT_TRUE(pool.set_description(state.cursor, "secret notes"));
    const auto vis = compute_visible(pool, state.folded);

    ftxui::Screen screen(100, 4);
    ftxui::Render(screen, render(pool, state, vis, 0, 100));

    EXPECT_EQ(screen.ToString().find("secret notes"), std::string::npos);
}

// --- title editing with a cursor ------------------------------------------------

namespace
{
ViewState insert_state(const std::string &buf, std::size_t cursor)
{
    ViewState state;
    state.mode = Mode::INSERT;
    state.edit_buffer = buf;
    state.edit_cursor = cursor;
    return state;
}
}  // namespace

TEST(ApplyCommand, BeginEditTitle_PrefillsTitleCursorAtEnd)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    ASSERT_TRUE(pool.set_title(state.cursor, "Ship v2"));
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdBeginEditTitle{.cursor_at_end = true}}, pool, state, vis));

    EXPECT_EQ(state.mode, Mode::INSERT);
    EXPECT_EQ(state.edit_field, EditField::TITLE);
    EXPECT_EQ(state.edit_buffer, "Ship v2");
    EXPECT_EQ(state.edit_cursor, 7u);
}

TEST(ApplyCommand, BeginEditTitle_CursorAtStart)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    ASSERT_TRUE(pool.set_title(state.cursor, "Ship v2"));
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdBeginEditTitle{.cursor_at_end = false}}, pool, state, vis));

    EXPECT_EQ(state.edit_buffer, "Ship v2");
    EXPECT_EQ(state.edit_cursor, 0u);
}

TEST(ApplyCommand, BeginEditTitle_WithoutCursorStaysNormal)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdBeginEditTitle{}}, pool, state, vis));

    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(InsertMode, ArrowsStepOverWholeMultibyteChar)
{
    NodePool pool;
    ViewState state = insert_state("añb", 4);  // end: a(1) ñ(2) b(1)
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::ArrowLeft);
    EXPECT_EQ(state.edit_cursor, 3u);
    (void)process_event(state, vis, ftxui::Event::ArrowLeft);
    EXPECT_EQ(state.edit_cursor, 1u);  // skipped both bytes of ñ
    (void)process_event(state, vis, ftxui::Event::ArrowLeft);
    EXPECT_EQ(state.edit_cursor, 0u);
    (void)process_event(state, vis, ftxui::Event::ArrowLeft);
    EXPECT_EQ(state.edit_cursor, 0u);  // clamps at start

    (void)process_event(state, vis, ftxui::Event::ArrowRight);
    (void)process_event(state, vis, ftxui::Event::ArrowRight);
    EXPECT_EQ(state.edit_cursor, 3u);
    (void)process_event(state, vis, ftxui::Event::ArrowRight);
    (void)process_event(state, vis, ftxui::Event::ArrowRight);
    EXPECT_EQ(state.edit_cursor, 4u);  // clamps at end
}

TEST(InsertMode, TypingInsertsAtCursor)
{
    NodePool pool;
    ViewState state = insert_state("helo", 3);
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character('l'));

    EXPECT_EQ(state.edit_buffer, "hello");
    EXPECT_EQ(state.edit_cursor, 4u);
}

TEST(InsertMode, BackspaceAndDeleteRemoveWholeMultibyteChar)
{
    NodePool pool;
    const auto vis = compute_visible(pool, FoldSet{});

    ViewState bs = insert_state("añb", 3);  // just after ñ
    (void)process_event(bs, vis, ftxui::Event::Backspace);
    EXPECT_EQ(bs.edit_buffer, "ab");
    EXPECT_EQ(bs.edit_cursor, 1u);

    ViewState del = insert_state("añb", 1);  // just before ñ
    (void)process_event(del, vis, ftxui::Event::Delete);
    EXPECT_EQ(del.edit_buffer, "ab");
    EXPECT_EQ(del.edit_cursor, 1u);

    ViewState at_start = insert_state("ab", 0);
    (void)process_event(at_start, vis, ftxui::Event::Backspace);
    EXPECT_EQ(at_start.edit_buffer, "ab");

    ViewState at_end = insert_state("ab", 2);
    (void)process_event(at_end, vis, ftxui::Event::Delete);
    EXPECT_EQ(at_end.edit_buffer, "ab");
}

TEST(InsertMode, HomeAndEndJump)
{
    NodePool pool;
    ViewState state = insert_state("abc", 1);
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::End);
    EXPECT_EQ(state.edit_cursor, 3u);
    (void)process_event(state, vis, ftxui::Event::Home);
    EXPECT_EQ(state.edit_cursor, 0u);
}

TEST(InsertMode, EditExistingTitleEndToEnd)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    ASSERT_TRUE(pool.set_title(state.cursor, "helo"));
    auto vis = compute_visible(pool, state.folded);

    auto result = process_event(state, vis, ftxui::Event::i);
    ASSERT_TRUE(result.command.has_value());
    ASSERT_TRUE(apply_command(*result.command, pool, state, vis));
    (void)process_event(state, vis, ftxui::Event::ArrowLeft);
    (void)process_event(state, vis, ftxui::Event::Character('l'));
    result = process_event(state, vis, ftxui::Event::Return);
    ASSERT_TRUE(result.command.has_value());
    ASSERT_TRUE(apply_command(*result.command, pool, state, vis));

    EXPECT_EQ(pool.get_title(state.cursor).value(), std::string_view{"hello"});
    EXPECT_EQ(state.mode, Mode::NORMAL);
    EXPECT_EQ(state.edit_cursor, 0u);
}

TEST(InsertMode, EscLeavesTitleUnchanged)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    ASSERT_TRUE(pool.set_title(state.cursor, "keep"));
    auto vis = compute_visible(pool, state.folded);

    auto result = process_event(state, vis, ftxui::Event::i);
    ASSERT_TRUE(apply_command(*result.command, pool, state, vis));
    (void)process_event(state, vis, ftxui::Event::Character('x'));
    (void)process_event(state, vis, ftxui::Event::Escape);

    EXPECT_EQ(pool.get_title(state.cursor).value(), std::string_view{"keep"});
    EXPECT_EQ(state.mode, Mode::NORMAL);
}

TEST(NewTask, OStartsWithEmptyBufferAndCursorZero)
{
    NodePool pool;
    ViewState state;
    state.cursor = pool.add_root();
    state.edit_buffer = "stale";
    state.edit_cursor = 5;
    auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdAddSiblingBelow{}}, pool, state, vis));

    EXPECT_EQ(state.mode, Mode::INSERT);
    EXPECT_TRUE(state.edit_buffer.empty());
    EXPECT_EQ(state.edit_cursor, 0u);
}

TEST(Render, StatusBarShowsTextOnBothSidesOfCursor)
{
    ViewState state = insert_state("abXYcd", 3);
    ftxui::Screen screen(30, 1);
    ftxui::Render(screen, render_status_bar(state));
    // The character under the cursor is styled, so the three parts are separate runs.
    const std::string out = screen.ToString();
    EXPECT_NE(out.find("abX"), std::string::npos);
    EXPECT_NE(out.find("Y"), std::string::npos);
    EXPECT_NE(out.find("cd"), std::string::npos);
    EXPECT_EQ(out.find("abXYcd"), std::string::npos);
}

TEST(DatePickerMode, I_PlacesCursorAtEndOfPrefill)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::DATE_PICKER;
    state.picker.view_epoch = 0;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::i);

    EXPECT_EQ(state.edit_cursor, state.edit_buffer.size());
}

}  // namespace toe::ui
