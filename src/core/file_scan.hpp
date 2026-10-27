#pragma once

#include <filesystem>
#include <vector>

namespace toe::core
{

struct ScanOptions
{
    int max_depth = 6;
};

// True when `path` is a readable file whose first four bytes are the toe magic.
[[nodiscard]] bool has_toe_magic(const std::filesystem::path &path);

// Collect toe files for the picker:
//   - `config_dir` (non-recursive): every `*.toe` or legacy `*.bin` with valid magic.
//   - `search_root` (recursive, up to `max_depth`): `*.toe` with valid magic.
//     Dot-directories and build/dependency directories are not entered, and
//     symlinks are not followed.
// Config-dir files come first, then search-root files, each group sorted by
// path; a file reachable through both is listed once. Filesystem errors skip
// the offending entry.
[[nodiscard]] std::vector<std::filesystem::path> find_toe_files(
    const std::filesystem::path &config_dir,
    const std::filesystem::path &search_root,
    ScanOptions options = {}
);

}  // namespace toe::core
