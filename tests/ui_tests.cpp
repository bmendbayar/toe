#include <gtest/gtest.h>

#include <ftxui/component/event.hpp>

#include "core/error.hpp"
#include "core/fields.hpp"
#include "core/node_pool.hpp"
#include "core/task_id.hpp"
#include "ui/commands.hpp"
#include "ui/key_handler.hpp"
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

TEST(ModeTransition, NormalI_EntersInsert)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);
    state.cursor = root;

    const auto result = process_event(state, vis, ftxui::Event::i);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::Insert);
}

TEST(ModeTransition, NormalColon_EntersCommand)
{
    NodePool pool;
    ViewState state;
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Character(':'));

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::Command);
}

TEST(ModeTransition, InsertEsc_ReturnsNormal)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Insert;
    state.edit_buffer = "hello";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Escape);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::Normal);
    EXPECT_TRUE(state.edit_buffer.empty());
}

TEST(ModeTransition, CommandEsc_ReturnsNormal)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Command;
    state.cmd_buffer = "wq";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Escape);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::Normal);
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
    state.mode = Mode::Insert;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character('h'));
    (void)process_event(state, vis, ftxui::Event::Character('i'));

    EXPECT_EQ(state.edit_buffer, "hi");
    EXPECT_EQ(state.mode, Mode::Insert);
}

TEST(InsertMode, Backspace_PopsLastChar)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Insert;
    state.edit_buffer = "abc";
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Backspace);

    EXPECT_EQ(state.edit_buffer, "ab");
}

TEST(InsertMode, Return_EmitsCmdCommitEdit_AndReturnToNormal)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Insert;
    state.edit_buffer = "Buy milk";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    ASSERT_TRUE(std::holds_alternative<CmdCommitEdit>(*result.command));
    EXPECT_EQ(std::get<CmdCommitEdit>(*result.command).text, "Buy milk");
    EXPECT_EQ(state.mode, Mode::Normal);
    EXPECT_TRUE(state.edit_buffer.empty());
}

// --- Command mode keymap ----------------------------------------------------------

TEST(CommandMode, CharAccumulates)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Command;
    const auto vis = compute_visible(pool, state.folded);

    (void)process_event(state, vis, ftxui::Event::Character('w'));

    EXPECT_EQ(state.cmd_buffer, "w");
}

TEST(CommandMode, Return_W_EmitsCmdSave)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Command;
    state.cmd_buffer = "w";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdSave>(*result.command));
    EXPECT_EQ(state.mode, Mode::Normal);
}

TEST(CommandMode, Return_Q_EmitsCmdQuit)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Command;
    state.cmd_buffer = "q";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    ASSERT_TRUE(result.command.has_value());
    EXPECT_TRUE(std::holds_alternative<CmdQuit>(*result.command));
}

TEST(CommandMode, Return_WQ_EmitsCmdSaveAndQuit)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Command;
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
    state.mode = Mode::Command;
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
    state.mode = Mode::Command;
    state.cmd_buffer = "foo";
    const auto vis = compute_visible(pool, state.folded);

    const auto result = process_event(state, vis, ftxui::Event::Return);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.command.has_value());
    EXPECT_EQ(state.mode, Mode::Normal);
}

TEST(CommandMode, Backspace_PopsLastChar)
{
    NodePool pool;
    ViewState state;
    state.mode = Mode::Command;
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
    EXPECT_EQ(state.mode, Mode::Insert);
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
    EXPECT_EQ(state.mode, Mode::Insert);
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
    EXPECT_EQ(state.mode, Mode::Insert);
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

    EXPECT_EQ(pool.get_status(a).value(), core::Status::Done);
}

TEST(ApplyCommand, ToggleDone_TogglesBackToTodo)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ASSERT_TRUE(pool.set_status(a, core::Status::Done).has_value());
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdToggleDone{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.get_status(a).value(), core::Status::Todo);
}

TEST(ApplyCommand, CyclePriority_AdvancesPriority)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdCyclePriority{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.get_priority(a).value(), core::Priority::Medium);
}

TEST(ApplyCommand, CycleStatus_AdvancesStatus)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(Command{CmdCycleStatus{}}, pool, state, vis).has_value());

    EXPECT_EQ(pool.get_status(a).value(), core::Status::InProgress);
}

TEST(ApplyCommand, CommitEdit_SetsDescription)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(apply_command(
                    Command{CmdCommitEdit{.text = "Buy milk"}}, pool, state, vis
    )
                    .has_value());

    EXPECT_EQ(pool.get_description(a).value(), "Buy milk");
}

TEST(ApplyCommand, CommitEdit_EmptyText_DoesNothing)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    ASSERT_TRUE(pool.set_description(a, "existing").has_value());
    ViewState state;
    state.cursor = a;
    const auto vis = compute_visible(pool, state.folded);

    ASSERT_TRUE(
        apply_command(Command{CmdCommitEdit{.text = ""}}, pool, state, vis)
            .has_value()
    );

    // description unchanged
    EXPECT_EQ(pool.get_description(a).value(), "existing");
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

}  // namespace toe::ui
