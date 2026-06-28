#pragma once

#include <cstdint>

namespace toe::core
{

// Core failure modes. Fallible operations report these through
// std::expected<T, Error>; exceptions are not part of the control-flow design.
enum class Error : std::uint8_t
{
    // A handle whose generation no longer matches its slot (freed/reused), or
    // whose index is out of range. The slot it once named is gone.
    StaleHandle,
};

}  // namespace toe::core
