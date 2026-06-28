#pragma once

#include <cstdint>
#include <type_traits>

namespace toe::core
{

// Stable handle into the node pool. Carries the slot `index` plus a per-slot
// `generation` so a freed-and-reused slot is distinguishable from a live one:
// a stale handle keeps the old generation and fails validation rather than
// silently aliasing whatever task now occupies the slot.
//
// `generation == 0` is reserved as the "never live" value, so a default-
// constructed TaskId is always invalid and live slots never carry it.
struct TaskId
{
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] friend constexpr bool operator==(TaskId, TaskId) = default;

    // Explicit invalid sentinel for call sites that want to spell intent.
    [[nodiscard]] static constexpr TaskId invalid() noexcept
    {
        return TaskId{};
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return generation != 0;
    }
};

// The handle must compile down to the two raw indices it carries: a zero-cost
// abstraction. Layout regressions fail the build here, not at runtime.
static_assert(sizeof(TaskId) == 8);
static_assert(alignof(TaskId) == 4);
static_assert(std::is_trivially_copyable_v<TaskId>);
static_assert(std::is_standard_layout_v<TaskId>);

}  // namespace toe::core
