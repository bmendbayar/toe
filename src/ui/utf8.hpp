#pragma once

#include <cstddef>
#include <string_view>

namespace toe::ui
{

[[nodiscard]] constexpr bool is_utf8_continuation(const char c) noexcept
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Byte offset of the character boundary before `pos` (0 if already at the start).
[[nodiscard]] constexpr std::size_t prev_boundary(const std::string_view s, std::size_t pos) noexcept
{
    pos = pos > s.size() ? s.size() : pos;
    if (pos == 0)
    {
        return 0;
    }
    --pos;
    while (pos > 0 && is_utf8_continuation(s[pos]))
    {
        --pos;
    }
    return pos;
}

// Byte offset of the character boundary after `pos` (s.size() if already at the end).
[[nodiscard]] constexpr std::size_t next_boundary(const std::string_view s, std::size_t pos) noexcept
{
    if (pos >= s.size())
    {
        return s.size();
    }
    ++pos;
    while (pos < s.size() && is_utf8_continuation(s[pos]))
    {
        ++pos;
    }
    return pos;
}

}  // namespace toe::ui
