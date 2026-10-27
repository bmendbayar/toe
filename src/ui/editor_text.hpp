#pragma once

#include <string>
#include <string_view>

namespace toe::ui
{

// Text read back from an external editor's temp file -> description text.
// Editors append a final newline the user did not type; drop exactly one so a
// no-op edit round-trips unchanged. Inner blank lines are preserved.
[[nodiscard]] inline std::string normalize_editor_text(std::string_view text)
{
    if (text.ends_with("\r\n"))
    {
        text.remove_suffix(2);
    }
    else if (text.ends_with('\n'))
    {
        text.remove_suffix(1);
    }
    return std::string{text};
}

}  // namespace toe::ui
