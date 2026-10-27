#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "app/file_picker.hpp"
#include "core/error.hpp"
#include "core/node_pool.hpp"
#include "core/serialization.hpp"
#include "ui/commands.hpp"
#include "ui/editor_text.hpp"
#include "ui/key_handler.hpp"
#include "ui/renderer.hpp"
#include "ui/scroll.hpp"
#include "ui/view_state.hpp"

namespace
{

void print_usage(std::FILE *out)
{
    std::fputs(
        "usage: toe [-f|--file <path>]\n"
        "  -f, --file <path>  load/save tasks from <path> and skip the file picker\n"
        "  -h, --help         show this help\n"
        "\n"
        "Without -f, toe opens a picker (sk, else fzf) over the *.toe files under the\n"
        "current directory and the *.toe / *.bin files in the config dir\n"
        "($XDG_CONFIG_HOME/toe or ~/.config/toe). Type a new name to create a file.\n"
        "If neither picker is installed, ~/.config/toe/tasks.bin is used.\n",
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

    const fs::path dest = toe::core::resolve_toe_path(target, toe::core::tasks_path().parent_path());
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

// Edit the cursor task's description in $VISUAL / $EDITOR (fallback vi) via a
// temp file, with the TUI suspended. Returns a status-line message ("" if
// nothing to report); sets `dirty` when the description changed.
std::string edit_description(
    ftxui::ScreenInteractive &screen,
    toe::core::NodePool &pool,
    toe::core::TaskId cursor,
    bool &dirty
)
{
    namespace fs = std::filesystem;
    const auto current_view = pool.get_description(cursor);
    if (!current_view)
    {
        return "nothing to edit";
    }
    const std::string current{*current_view};  // copy: set_description invalidates views

    std::string tmpl = (fs::temp_directory_path() / "toe-XXXXXX.md").string();
    const int fd = ::mkstemps(tmpl.data(), 3);
    if (fd < 0)
    {
        return "couldn't create temp file for editor";
    }
    ::close(fd);
    const fs::path tmp{tmpl};
    const auto cleanup = [&] {
        std::error_code ec;
        fs::remove(tmp, ec);
    };

    {
        std::ofstream os{tmp, std::ios::binary | std::ios::trunc};
        os << current;
        if (!os)
        {
            cleanup();
            return "couldn't write temp file for editor";
        }
    }

    const char *visual = std::getenv("VISUAL");
    const char *editor_env = std::getenv("EDITOR");
    const std::string editor = (visual != nullptr && *visual != '\0')           ? visual
                               : (editor_env != nullptr && *editor_env != '\0') ? editor_env
                                                                                : "vim";
    const std::string command = editor + " " + toe::app::shell_quote(tmp.string());

    int rc = -1;
    screen.WithRestoredIO([&] { rc = std::system(command.c_str()); })();
    if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        cleanup();
        return "editor exited with error — description unchanged";
    }

    std::ifstream is{tmp, std::ios::binary};
    if (!is)
    {
        cleanup();
        return "couldn't read back edited description";
    }
    const std::string raw{std::istreambuf_iterator<char>{is}, std::istreambuf_iterator<char>{}};
    cleanup();

    const std::string edited = toe::ui::normalize_editor_text(raw);
    if (edited == current)
    {
        return "";
    }
    if (auto r = pool.set_description(cursor, edited); !r)
    {
        return r.error() == toe::core::Error::DESCRIPTION_TOO_LONG
                   ? "description too long (max 64 KiB) — unchanged"
                   : "couldn't save description";
    }
    dirty = true;
    return "";
}

// Switch to another toe file (`;open[!] [target]`). An empty target runs the
// picker with the TUI suspended. Returns the status-line message.
std::string open_file(
    ftxui::ScreenInteractive &screen,
    toe::core::NodePool &pool,
    toe::ui::ViewState &state,
    bool &dirty,
    std::filesystem::path &path,
    const toe::ui::CmdOpen &cmd
)
{
    namespace fs = std::filesystem;
    if (dirty && !cmd.force)
    {
        return "unsaved changes — ;w to save, ;open! to discard";
    }

    const fs::path config_dir = toe::core::tasks_path().parent_path();
    fs::path target;
    if (cmd.target.empty())
    {
        std::error_code ec;
        const fs::path cwd = fs::current_path(ec);
        std::expected<toe::ui::PickResult, std::string> picked =
            std::unexpected(std::string{"picker didn't run"});
        screen.WithRestoredIO([&] { picked = toe::app::run_picker(config_dir, cwd); })();
        if (!picked)
        {
            return picked.error();
        }
        const auto *choice = std::get_if<toe::ui::PickPath>(&*picked);
        if (choice == nullptr)
        {
            return "";
        }
        target = choice->path;
    }
    else
    {
        target = toe::core::resolve_toe_path(cmd.target, config_dir);
    }

    toe::core::NodePool next;
    bool created = false;
    if (auto r = toe::core::load(target); r)
    {
        next = std::move(*r);
    }
    else if (r.error() == toe::core::Error::IO_ERROR)
    {
        std::error_code ec;
        if (fs::exists(target, ec))
        {
            return "couldn't read " + target.string();
        }
        created = true;  // missing file: start empty, created on ;w
    }
    else
    {
        return "open failed: " + target.string() + " is not a valid toe file";
    }

    pool = std::move(next);
    path = std::move(target);
    state = toe::ui::ViewState{};
    if (const auto root = pool.root(); root.is_valid())
    {
        state.cursor = root;
    }
    dirty = false;
    return (created ? "new file " : "opened ") + path.string();
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

    std::filesystem::path path;
    if (args.file)
    {
        path = *args.file;
    }
    else
    {
        path = toe::core::tasks_path();
        if (::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0)
        {
            std::error_code ec;
            const auto cwd = std::filesystem::current_path(ec);
            const auto picked = toe::app::run_picker(path.parent_path(), cwd);
            if (!picked)
            {
                std::fprintf(stderr, "toe: %s — opening %s\n", picked.error().c_str(), path.c_str());
            }
            else if (const auto *choice = std::get_if<toe::ui::PickPath>(&*picked))
            {
                path = choice->path;
            }
            else
            {
                return 0;  // picker cancelled
            }
        }
    }
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
        const int screen_cols = ftxui::Terminal::Size().dimx;
        const int reserved = (status_message.empty() ? 1 : 2) +  // mode bar (+ message line)
                             toe::ui::details_pane_rows(state, screen_cols);
        const int body_rows = std::max(0, ftxui::Terminal::Size().dimy - reserved);
        toe::ui::update_tree_scroll(state, visible, static_cast<std::size_t>(body_rows));
        auto body = toe::ui::render(pool, state, visible, now_epoch, screen_cols);
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
        else if (std::holds_alternative<toe::ui::CmdEditDescription>(cmd))
        {
            status_message = edit_description(screen, pool, state.cursor, dirty);
        }
        else if (const auto *open = std::get_if<toe::ui::CmdOpen>(&cmd))
        {
            status_message = open_file(screen, pool, state, dirty, path, *open);
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
