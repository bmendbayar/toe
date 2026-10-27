#include "app/file_picker.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "core/file_scan.hpp"

namespace toe::app
{

namespace fs = std::filesystem;

std::string shell_quote(const std::string &s)
{
    std::string out = "'";
    for (const char c : s)
    {
        if (c == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out += c;
        }
    }
    out += '\'';
    return out;
}

std::optional<std::string> find_picker()
{
    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr)
    {
        return std::nullopt;
    }
    for (const char *name : {"sk", "fzf"})
    {
        std::istringstream dirs{path_env};
        for (std::string dir; std::getline(dirs, dir, ':');)
        {
            const fs::path candidate = fs::path{dir.empty() ? "." : dir} / name;
            if (::access(candidate.c_str(), X_OK) == 0)
            {
                return std::string{name};
            }
        }
    }
    return std::nullopt;
}

namespace
{

// Remove the temp files on every exit path.
struct TempFile
{
    fs::path path;

    static std::optional<TempFile> create()
    {
        std::string tmpl = (fs::temp_directory_path() / "toe-pick-XXXXXX").string();
        const int fd = ::mkstemp(tmpl.data());
        if (fd < 0)
        {
            return std::nullopt;
        }
        ::close(fd);
        return TempFile{fs::path{tmpl}};
    }

    TempFile(TempFile &&other) noexcept : path(std::move(other.path)) { other.path.clear(); }
    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;
    TempFile &operator=(TempFile &&) = delete;

    ~TempFile()
    {
        if (!path.empty())
        {
            std::error_code ec;
            fs::remove(path, ec);
        }
    }

private:
    explicit TempFile(fs::path p) : path(std::move(p)) {}
};

}  // namespace

std::expected<ui::PickResult, std::string>
run_picker(const fs::path &config_dir, const fs::path &cwd)
{
    const auto picker = find_picker();
    if (!picker)
    {
        return std::unexpected("no file picker found — install sk or fzf");
    }

    const char *home_env = std::getenv("HOME");
    const fs::path home = (home_env != nullptr) ? fs::path{home_env} : fs::path{};

    std::map<std::string, fs::path> display_to_path;
    for (const auto &file : core::find_toe_files(config_dir, cwd))
    {
        display_to_path.emplace(ui::display_name(file, cwd, home), file);
    }

    auto in_file = TempFile::create();
    auto out_file = TempFile::create();
    if (!in_file || !out_file)
    {
        return std::unexpected("couldn't create temp files for the picker");
    }
    {
        std::ofstream os{in_file->path, std::ios::trunc};
        for (const auto &entry : display_to_path)
        {
            os << entry.first << '\n';
        }
        if (!os)
        {
            return std::unexpected("couldn't write the picker's input file");
        }
    }

    const std::string command = *picker + " --print-query --margin 10% --color=bw --prompt " +
                                shell_quote("toe> ") + " < " + shell_quote(in_file->path.string()) +
                                " > " + shell_quote(out_file->path.string());
    const int rc = std::system(command.c_str());
    const int exit_code = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;

    std::ifstream is{out_file->path};
    const std::string output{std::istreambuf_iterator<char>{is}, std::istreambuf_iterator<char>{}};
    return ui::parse_picker_output(output, exit_code, display_to_path, config_dir);
}

}  // namespace toe::app
