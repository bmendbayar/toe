#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>

#include "core/serialization.hpp"

namespace toe::ui
{

// Pure helpers for the external file picker (sk / fzf); the process handling
// lives in app/file_picker.cpp.

struct PickCancelled {};
struct PickPath { std::filesystem::path path; };
using PickResult = std::variant<PickCancelled, PickPath>;

// Label shown in the picker: relative to `cwd` for files beneath it, `~/...`
// for files beneath `home`, otherwise the absolute path.
[[nodiscard]] inline std::string display_name(
    const std::filesystem::path &path,
    const std::filesystem::path &cwd,
    const std::filesystem::path &home
)
{
    namespace fs = std::filesystem;
    const auto beneath = [&](const fs::path &base) -> std::string {
        if (base.empty())
        {
            return {};
        }
        const fs::path rel = path.lexically_relative(base);
        if (rel.empty() || *rel.begin() == "..")
        {
            return {};
        }
        return rel.string();
    };

    if (auto rel = beneath(cwd); !rel.empty())
    {
        return rel;
    }
    if (auto rel = beneath(home); !rel.empty())
    {
        return "~/" + rel;
    }
    return path.string();
}

// Interpret a picker run started with `--print-query`: line 1 of `output` is
// the typed query, line 2 the highlighted entry.
//   exit 0, entry known     -> that file
//   exit 1 (no match), query -> a new file named by the query
//   anything else           -> cancelled
[[nodiscard]] inline PickResult parse_picker_output(
    std::string_view output,
    int exit_code,
    const std::map<std::string, std::filesystem::path> &display_to_path,
    const std::filesystem::path &config_dir
)
{
    const auto next_line = [&output]() -> std::string {
        const auto nl = output.find('\n');
        std::string line{output.substr(0, nl)};
        output = nl == std::string_view::npos ? std::string_view{} : output.substr(nl + 1);
        return line;
    };
    const std::string query = next_line();
    const std::string selection = next_line();

    if (exit_code == 0)
    {
        if (const auto it = display_to_path.find(selection); it != display_to_path.end())
        {
            return PickPath{it->second};
        }
        return PickCancelled{};
    }
    if (exit_code == 1 && !query.empty())
    {
        return PickPath{core::resolve_toe_path(query, config_dir)};
    }
    return PickCancelled{};
}

}  // namespace toe::ui
