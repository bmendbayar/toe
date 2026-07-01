#include <chrono>
#include <cstdio>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <variant>

#include "core/error.hpp"
#include "core/node_pool.hpp"
#include "core/serialization.hpp"
#include "ui/commands.hpp"
#include "ui/key_handler.hpp"
#include "ui/renderer.hpp"
#include "ui/view_state.hpp"

int main()
{
    const auto path = toe::core::tasks_path();
    toe::core::NodePool pool;

    if (auto r = toe::core::load(path); r)
    {
        pool = std::move(*r);
    }
    else if (r.error() != toe::core::Error::IO_ERROR)
    {
        // Corrupt save file — abort rather than silently overwrite user data.
        std::fprintf(stderr, "toe: corrupt save file at %s\n", path.c_str());
        return 1;
    }
    // IoError = missing file; start with a fresh empty pool.

    toe::ui::ViewState state;
    if (const auto root = pool.root(); root.is_valid())
    {
        state.cursor = root;
    }

    bool dirty = false;
    std::string status_message;

    auto screen = ftxui::ScreenInteractive::FullscreenAlternateScreen();

    auto exit = screen.ExitLoopClosure();

    auto renderer = ftxui::Renderer([&] {
        const auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch()
        )
                                   .count();
        const auto visible = toe::ui::compute_visible(pool, state.folded);
        auto body = toe::ui::render(pool, state, visible, now_epoch);
        auto status = toe::ui::render_status_bar(state);
        if (status_message.empty())
        {
            return ftxui::vbox({std::move(body) | ftxui::flex, std::move(status)});
        }
        return ftxui::vbox({
            std::move(body) | ftxui::flex,
            std::move(status),
            ftxui::text(status_message) | ftxui::color(ftxui::Color::Yellow),
        });
    });

    auto component = ftxui::CatchEvent(renderer, [&](const ftxui::Event &event) -> bool {
        status_message.clear();

        const auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch()
        )
                                   .count();
        const auto visible = toe::ui::compute_visible(pool, state.folded);
        auto result = toe::ui::process_event(state, visible, event, now_epoch);
        if (!result.consumed)
        {
            return false;
        }
        if (!result.command)
        {
            return true;
        }

        const toe::ui::Command &cmd = *result.command;

        if (std::holds_alternative<toe::ui::CmdQuit>(cmd))
        {
            if (dirty)
            {
                status_message = "unsaved changes — :w to save, :q! to discard";
            }
            else
            {
                exit();
            }
        }
        else if (std::holds_alternative<toe::ui::CmdForceQuit>(cmd))
        {
            exit();
        }
        else if (std::holds_alternative<toe::ui::CmdSave>(cmd))
        {
            if (auto r = toe::core::save(pool, path); r)
            {
                dirty = false;
            }
            else
            {
                status_message = "save failed — check disk space or permissions";
            }
        }
        else if (std::holds_alternative<toe::ui::CmdSaveAndQuit>(cmd))
        {
            if (auto r = toe::core::save(pool, path); r)
            {
                dirty = false;
                exit();
            }
            else
            {
                status_message = "save failed — check disk space or permissions";
            }
        }
        else
        {
            if (auto r = toe::ui::apply_command(cmd, pool, state, visible, now_epoch); r)
            {
                dirty = true;
            }
        }

        return true;
    });

    screen.Loop(component);
    return 0;
}
