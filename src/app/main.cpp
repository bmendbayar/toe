#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "core/error.hpp"
#include "core/node_pool.hpp"
#include "core/serialization.hpp"
#include "ui/commands.hpp"
#include "ui/key_handler.hpp"
#include "ui/renderer.hpp"
#include "ui/view_state.hpp"
#include "ui/scroll.hpp"

namespace
{

void print_usage(std::FILE *out)
{
    std::fputs(
        "usage: toe [-f|--file <path>]\n"
        "  -f, --file <path>  load/save tasks from <path> instead of the default\n"
        "                     ($XDG_CONFIG_HOME/toe/tasks.bin or ~/.config/toe/tasks.bin)\n"
        "  -h, --help         show this help\n",
        out
    );
}

struct Args
{
    std::optional<std::filesystem::path> file;
    bool help = false;
    bool ok = true;
};

Args parse_args(int argc, char **argv)
{
    Args args;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            args.help = true;
        }
        else if (arg == "-f" || arg == "--file")
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "toe: %s requires a path\n", argv[i]);
                args.ok = false;
                return args;
            }
            args.file = std::filesystem::path{argv[++i]};
        }
        else if (arg.starts_with("--file="))
        {
            args.file = std::filesystem::path{std::string{arg.substr(7)}};
        }
        else
        {
            std::fprintf(stderr, "toe: unknown argument '%s'\n", argv[i]);
            args.ok = false;
            return args;
        }
    }
    if (args.file && args.file->empty())
    {
        std::fprintf(stderr, "toe: empty file path\n");
        args.ok = false;
    }
    return args;
}

// Copy the subtree under `cursor` onto the end of the toe file named by `target`
// (created if missing). Returns the status-line message describing the outcome.
std::string export_subtree(
    const toe::core::NodePool &pool,
    toe::core::TaskId cursor,
    const std::filesystem::path &current_file,
    const std::string &target
)
{
    namespace fs = std::filesystem;
    if (target.empty())
    {
        return "usage: ;export <name|path>";
    }
    if (!pool.is_valid(cursor))
    {
        return "nothing to export";
    }

    const fs::path dest = toe::core::resolve_export_path(target, toe::core::tasks_path().parent_path());
    std::error_code ec;
    // equivalent() fails when either file is missing; fall back to a lexical compare.
    if (fs::equivalent(dest, current_file, ec) || dest.lexically_normal() == current_file.lexically_normal())
    {
        return "can't export into the file being edited";
    }

    toe::core::NodePool dest_pool;
    if (auto r = toe::core::load(dest); r)
    {
        dest_pool = std::move(*r);
    }
    else if (r.error() != toe::core::Error::IO_ERROR)
    {
        return "export failed: " + dest.string() + " is not a valid toe file";
    }
    // IoError = missing file; start a fresh destination pool.

    if (auto r = toe::core::copy_subtree(pool, cursor, dest_pool); !r)
    {
        return "export failed — could not copy subtree";
    }
    if (auto r = toe::core::save(dest_pool, dest); !r)
    {
        return "export failed — check disk space or permissions";
    }
    return "exported subtree to " + dest.string();
}

}  // namespace

int main(int argc, char **argv)
{
    const Args args = parse_args(argc, argv);
    if (!args.ok)
    {
        print_usage(stderr);
        return 2;
    }
    if (args.help)
    {
        print_usage(stdout);
        return 0;
    }

    const auto path = args.file ? *args.file : toe::core::tasks_path();
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
        // Terminal::Size() rather than screen.dimy(): the screen resizes after Render().
        const int reserved = status_message.empty() ? 1 : 2;  // mode bar (+ message line)
        const int body_rows = std::max(0, ftxui::Terminal::Size().dimy - reserved);
        toe::ui::update_tree_scroll(state, visible, static_cast<std::size_t>(body_rows));
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
        else if (const auto *exp = std::get_if<toe::ui::CmdExport>(&cmd))
        {
            status_message = export_subtree(pool, state.cursor, path, exp->target);
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
