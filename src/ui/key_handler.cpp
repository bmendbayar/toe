#include <algorithm>
#include <iterator>

#include "core/fields.hpp"
#include "ui/date_util.hpp"
#include "ui/key_handler.hpp"

namespace toe::ui
{

std::vector<core::TaskId>
compute_visible(const core::NodePool &pool, const FoldSet &folded)
{
    std::vector<core::TaskId> order;
    const core::TaskId root = pool.root();
    if (!root.is_valid())
    {
        return order;
    }
    order.reserve(pool.live_count());

    // Same push-sibling-before-child trick as NodePool::flatten, but skip
    // pushing children when the node is in the fold set.
    std::vector<core::TaskId> stack;
    stack.push_back(root);
    while (!stack.empty())
    {
        const core::TaskId node = stack.back();
        stack.pop_back();
        order.push_back(node);

        const core::TaskId sibling =
            pool.next_sibling(node).value_or(core::TaskId::invalid());
        if (sibling.is_valid())
        {
            stack.push_back(sibling);
        }
        if (!folded.contains(node))
        {
            const core::TaskId child =
                pool.first_child(node).value_or(core::TaskId::invalid());
            if (child.is_valid())
            {
                stack.push_back(child);
            }
        }
    }
    return order;
}

// --- Mode-specific handlers --------------------------------------------------

// Returns the index of state.cursor in visible, or visible.size() if absent.
[[nodiscard]] std::size_t cursor_index(const ViewState &state, const std::vector<core::TaskId> &visible)
{
    const auto it = std::ranges::find(visible, state.cursor);
    return static_cast<std::size_t>(it - visible.begin());
}

EventResult handle_normal(ViewState &state, const std::vector<core::TaskId> &visible, const ftxui::Event &event)
{
    const auto reset_d = [&] { state.pending_d = false; };

    // j / ArrowDown - move cursor down one visible row
    if (event == ftxui::Event::j || event == ftxui::Event::ArrowDown)
    {
        reset_d();
        if (!visible.empty())
        {
            const std::size_t idx = cursor_index(state, visible);
            const std::size_t next =
                (idx + 1 < visible.size()) ? idx + 1 : visible.size() - 1;
            state.cursor = visible[next];
        }
        return {.consumed = true, .command = std::nullopt};
    }

    // k / ArrowUp - move cursor up one visible row
    if (event == ftxui::Event::k || event == ftxui::Event::ArrowUp)
    {
        reset_d();
        if (!visible.empty())
        {
            const std::size_t idx = cursor_index(state, visible);
            const std::size_t prev = (idx > 0) ? idx - 1 : 0;
            state.cursor = visible[prev];
        }
        return {.consumed = true, .command = std::nullopt};
    }

    // g - jump to first visible task
    if (event == ftxui::Event::g)
    {
        reset_d();
        if (!visible.empty())
        {
            state.cursor = visible.front();
        }
        return {.consumed = true, .command = std::nullopt};
    }

    // G - jump to last visible task
    if (event == ftxui::Event::G)
    {
        reset_d();
        if (!visible.empty())
        {
            state.cursor = visible.back();
        }
        return {.consumed = true, .command = std::nullopt};
    }

    // h / ArrowLeft - fold (collapse children)
    if (event == ftxui::Event::h || event == ftxui::Event::ArrowLeft)
    {
        reset_d();
        if (state.cursor.is_valid())
        {
            state.folded.insert(state.cursor);
        }
        return {.consumed = true, .command = std::nullopt};
    }

    // l / ArrowRight - unfold (expand children)
    if (event == ftxui::Event::l || event == ftxui::Event::ArrowRight)
    {
        reset_d();
        if (state.cursor.is_valid())
        {
            state.folded.erase(state.cursor);
        }
        return {.consumed = true, .command = std::nullopt};
    }

    // i - enter Insert mode to edit description
    if (event == ftxui::Event::i)
    {
        reset_d();
        state.mode = Mode::Insert;
        state.edit_field = EditField::Description;
        state.edit_buffer.clear();
        return {.consumed = true, .command = std::nullopt};
    }

    // o - open new sibling below
    if (event == ftxui::Event::o)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdAddSiblingBelow{}}};
    }

    // O - open new sibling above
    if (event == ftxui::Event::O)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdAddSiblingAbove{}}};
    }

    // Tab - indent (re-parent under preceding sibling)
    if (event == ftxui::Event::Tab)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdIndent{}}};
    }

    // Shift+Tab - outdent (lift to grandparent level)
    if (event == ftxui::Event::TabReverse)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdOutdent{}}};
    }

    // x - toggle Done <-> Todo
    if (event == ftxui::Event::x)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdToggleDone{}}};
    }

    // D - enter Insert mode to edit due date (pre-populated if one is set)
    if (event == ftxui::Event::D)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdBeginEditDueDate{}}};
    }

    // d / dd - delete task (two-key sequence)
    if (event == ftxui::Event::d)
    {
        if (state.pending_d)
        {
            state.pending_d = false;
            return {.consumed = true, .command = Command{CmdDeleteTask{}}};
        }
        state.pending_d = true;
        return {.consumed = true, .command = std::nullopt};
    }

    // p - cycle priority
    if (event == ftxui::Event::p)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdCyclePriority{}}};
    }

    // s - cycle status
    if (event == ftxui::Event::s)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdCycleStatus{}}};
    }

    // : - enter Command mode
    if (event == ftxui::Event::Character(':'))
    {
        reset_d();
        state.mode = Mode::Command;
        state.cmd_buffer.clear();
        return {.consumed = true, .command = std::nullopt};
    }

    // q - quit
    if (event == ftxui::Event::q)
    {
        reset_d();
        return {.consumed = true, .command = Command{CmdQuit{}}};
    }

    // Esc - cancel pending 'd' sequence
    if (event == ftxui::Event::Escape)
    {
        reset_d();
        return {.consumed = true, .command = std::nullopt};
    }

    reset_d();
    return {.consumed = false, .command = std::nullopt};
}

EventResult handle_insert(ViewState &state, const ftxui::Event &event)
{
    if (event == ftxui::Event::Escape)
    {
        state.mode = Mode::Normal;
        state.edit_field = EditField::Description;
        state.edit_buffer.clear();
        return {.consumed = true, .command = std::nullopt};
    }

    if (event == ftxui::Event::Return)
    {
        std::string text = std::move(state.edit_buffer);
        state.edit_buffer.clear();
        state.mode = Mode::Normal;
        const EditField field = state.edit_field;
        state.edit_field = EditField::Description;
        if (field == EditField::DueDate)
        {
            return {.consumed = true, .command = Command{CmdCommitDueDate{.text = std::move(text)}}};
        }
        return {.consumed = true, .command = Command{CmdCommitEdit{.text = std::move(text)}}};
    }

    if (event == ftxui::Event::Backspace)
    {
        if (!state.edit_buffer.empty())
        {
            state.edit_buffer.pop_back();
        }
        return {.consumed = true, .command = std::nullopt};
    }

    if (event.is_character())
    {
        state.edit_buffer += event.character();
        return {.consumed = true, .command = std::nullopt};
    }

    return {.consumed = false, .command = std::nullopt};
}

EventResult handle_command(ViewState &state, const ftxui::Event &event)
{
    if (event == ftxui::Event::Escape)
    {
        state.mode = Mode::Normal;
        state.cmd_buffer.clear();
        return {.consumed = true, .command = std::nullopt};
    }

    if (event == ftxui::Event::Return)
    {
        std::string cmd = std::move(state.cmd_buffer);
        state.cmd_buffer.clear();
        state.mode = Mode::Normal;

        std::optional<Command> command;
        if (cmd == "w")
        {
            command = Command{CmdSave{}};
        }
        else if (cmd == "q")
        {
            command = Command{CmdQuit{}};
        }
        else if (cmd == "wq" || cmd == "x")
        {
            command = Command{CmdSaveAndQuit{}};
        }
        else if (cmd == "q!")
        {
            command = Command{CmdForceQuit{}};
        }
        return {.consumed = true, .command = std::move(command)};
    }

    if (event == ftxui::Event::Backspace)
    {
        if (!state.cmd_buffer.empty())
        {
            state.cmd_buffer.pop_back();
        }
        return {.consumed = true, .command = std::nullopt};
    }

    if (event.is_character())
    {
        state.cmd_buffer += event.character();
        return {.consumed = true, .command = std::nullopt};
    }

    return {.consumed = false, .command = std::nullopt};
}

// --- Public interface --------------------------------------------------

EventResult process_event(ViewState &state, const std::vector<core::TaskId> &visible, const ftxui::Event &event)
{
    switch (state.mode)
    {
        case Mode::Normal:
            return handle_normal(state, visible, event);
        case Mode::Insert:
            return handle_insert(state, event);
        case Mode::Command:
            return handle_command(state, event);
    }
    return {.consumed = false, .command = std::nullopt};
}

std::expected<void, core::Error>
apply_command(const Command &cmd, core::NodePool &pool, ViewState &state, const std::vector<core::TaskId> &visible)
{
    return std::visit(
        [&]([[maybe_unused]] auto &&c) -> std::expected<void, core::Error> {
            using T = std::decay_t<decltype(c)>;

            if constexpr (std::is_same_v<T, CmdAddSiblingBelow>)
            {
                if (!state.cursor.is_valid())
                {
                    state.cursor = pool.add_root();
                }
                else
                {
                    auto result = pool.add_sibling_after(state.cursor);
                    if (!result)
                    {
                        return std::unexpected(result.error());
                    }
                    state.cursor = *result;
                }
                state.mode = Mode::Insert;
                state.edit_field = EditField::Description;
                state.edit_buffer.clear();
                return {};
            }

            if constexpr (std::is_same_v<T, CmdAddSiblingAbove>)
            {
                if (!state.cursor.is_valid())
                {
                    state.cursor = pool.add_root();
                }
                else
                {
                    auto result = pool.add_sibling_before(state.cursor);
                    if (!result)
                    {
                        return std::unexpected(result.error());
                    }
                    state.cursor = *result;
                }
                state.mode = Mode::Insert;
                state.edit_field = EditField::Description;
                state.edit_buffer.clear();
                return {};
            }

            if constexpr (std::is_same_v<T, CmdIndent>)
            {
                if (!state.cursor.is_valid())
                {
                    return {};
                }
                return pool.indent(state.cursor);
            }

            if constexpr (std::is_same_v<T, CmdOutdent>)
            {
                if (!state.cursor.is_valid())
                {
                    return {};
                }
                return pool.outdent(state.cursor);
            }

            if constexpr (std::is_same_v<T, CmdDeleteTask>)
            {
                if (!state.cursor.is_valid())
                {
                    return {};
                }
                const auto it = std::ranges::find(visible, state.cursor);
                core::TaskId fallback;
                if (it != visible.end())
                {
                    // Walk forward in DFS order to find the first visible task
                    // that is NOT a descendant of cursor (i.e. cursor does not
                    // appear in its parent chain). Descendants appear
                    // contiguously after cursor in pre-order, so the first
                    // non-descendant is the natural successor.
                    for (auto cand = std::next(it); cand != visible.end(); ++cand)
                    {
                        bool in_subtree = false;
                        core::TaskId p = *cand;
                        while (p.is_valid())
                        {
                            const auto par = pool.parent(p);
                            if (!par.has_value())
                            {
                                break;
                            }
                            p = par.value();
                            if (p == state.cursor)
                            {
                                in_subtree = true;
                                break;
                            }
                        }
                        if (!in_subtree)
                        {
                            fallback = *cand;
                            break;
                        }
                    }
                    // Nothing usable after - fall back to the task before cursor.
                    if (!fallback.is_valid() && it != visible.begin())
                    {
                        fallback = *std::prev(it);
                    }
                }
                if (auto res = pool.remove_subtree(state.cursor); !res)
                {
                    return std::unexpected(res.error());
                }
                state.cursor =
                    pool.is_valid(fallback) ? fallback : core::TaskId::invalid();
                return {};
            }

            if constexpr (std::is_same_v<T, CmdToggleDone>)
            {
                if (!state.cursor.is_valid())
                {
                    return {};
                }
                auto status = pool.get_status(state.cursor);
                if (!status)
                {
                    return std::unexpected(status.error());
                }
                const core::Status next = (*status == core::Status::Done)
                                              ? core::Status::Todo
                                              : core::Status::Done;
                return pool.set_status(state.cursor, next);
            }

            if constexpr (std::is_same_v<T, CmdCyclePriority>)
            {
                if (!state.cursor.is_valid())
                {
                    return {};
                }
                if (auto res = pool.advance_priority(state.cursor); !res)
                {
                    return std::unexpected(res.error());
                }
                return {};
            }

            if constexpr (std::is_same_v<T, CmdCycleStatus>)
            {
                if (!state.cursor.is_valid())
                {
                    return {};
                }
                if (auto res = pool.advance_status(state.cursor); !res)
                {
                    return std::unexpected(res.error());
                }
                return {};
            }

            if constexpr (std::is_same_v<T, CmdCommitEdit>)
            {
                if (!state.cursor.is_valid() || c.text.empty())
                {
                    return {};
                }
                return pool.set_description(state.cursor, c.text);
            }

            if constexpr (std::is_same_v<T, CmdBeginEditDueDate>)
            {
                if (!state.cursor.is_valid())
                {
                    return {};
                }
                const auto date_result = pool.get_due_date(state.cursor);
                if (!date_result)
                {
                    return std::unexpected(date_result.error());
                }
                state.edit_buffer.clear();
                if (date_result->has_value())
                {
                    state.edit_buffer = format_due_date(**date_result);
                }
                state.edit_field = EditField::DueDate;
                state.mode = Mode::Insert;
                return {};
            }

            if constexpr (std::is_same_v<T, CmdCommitDueDate>)
            {
                if (!state.cursor.is_valid())
                {
                    return {};
                }
                if (c.text.empty())
                {
                    return pool.set_due_date(state.cursor, std::nullopt);
                }
                const auto epoch = parse_due_date(c.text);
                if (!epoch)
                {
                    return {};  // invalid format — no-op, leave date unchanged
                }
                return pool.set_due_date(state.cursor, *epoch);
            }

            // CmdQuit, CmdForceQuit, CmdSave, CmdSaveAndQuit: no Core mutation; app handles them.
            return {};
        },
        cmd
    );
}

}  // namespace toe::ui
