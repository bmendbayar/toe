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

    // A reparent that would attach a node to itself or to one of its own
    // descendants, which would splice the forest into a cycle. Rejected rather
    // than executed, so traversal can never loop forever.
    WouldCreateCycle,

    // Indent requires a preceding sibling to become the new parent; the node is
    // first among its siblings, so there is nothing to indent under.
    NoPreviousSibling,

    // Outdent lifts a node to its grandparent's level; a root has no parent and
    // therefore cannot be outdented further.
    NoParent,
};

}  // namespace toe::core
