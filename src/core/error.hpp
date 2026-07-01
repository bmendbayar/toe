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
    STALE_HANDLE,

    // A reparent that would attach a node to itself or to one of its own
    // descendants, which would splice the forest into a cycle. Rejected rather
    // than executed, so traversal can never loop forever.
    WOULD_CREATE_CYCLE,

    // Indent requires a preceding sibling to become the new parent; the node is
    // first among its siblings, so there is nothing to indent under.
    NO_PREVIOUS_SIBLING,

    // Outdent lifts a node to its grandparent's level; a root has no parent and
    // therefore cannot be outdented further.
    NO_PARENT,

    // set_description was called with an empty string_view. The description
    // field requires at least one character; use a separate clear operation to
    // remove an existing description.
    EMPTY_DESCRIPTION,

    // set_description was called with a string longer than kMaxDescriptionBytes.
    // The arena append is rejected so the old description (if any) is preserved.
    DESCRIPTION_TOO_LONG,

    // The serialized blob does not begin with the expected 4-byte magic. The
    // file is not a toe save (or its first bytes are clobbered).
    BAD_MAGIC,

    // The magic matched but the version field names a format this build does
    // not understand. Refused rather than misread under the wrong layout.
    UNSUPPORTED_VERSION,

    // The blob ended before a declared field/array could be fully read: the
    // header promises more bytes than the buffer holds. A short/cut-off file.
    TRUNCATED_DATA,

    // The blob is the right length but internally inconsistent: a stored handle
    // or description slice points out of range, the free list is malformed, an
    // enum is out of range, or trailing bytes remain. Refused, never trusted.
    CORRUPT_DATA,

    // A filesystem operation (open/read/write/create-dir) failed. Distinct from
    // a malformed blob: the bytes were never the problem.
    IO_ERROR,
};

}  // namespace toe::core
