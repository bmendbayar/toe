#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "ui/picker.hpp"

namespace toe::app
{

// POSIX single-quote escaping so a path survives `sh -c`.
[[nodiscard]] std::string shell_quote(const std::string &s);

// First of `sk`, `fzf` found on $PATH (executable), if any.
[[nodiscard]] std::optional<std::string> find_picker();

// Scan `config_dir` and `cwd` for toe files and let the user choose one with
// sk/fzf (new names are allowed). The picker draws on /dev/tty, so the caller
// must have the TUI suspended. Returns the error text when no picker is
// installed or temp files can't be created.
[[nodiscard]] std::expected<ui::PickResult, std::string>
run_picker(const std::filesystem::path &config_dir, const std::filesystem::path &cwd);

}  // namespace toe::app
