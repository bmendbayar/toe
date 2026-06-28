#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace toe::core
{

enum class Status : std::uint8_t
{
    Todo,
    InProgress,
    Done,
};

enum class Priority : std::uint8_t
{
    Low,
    Medium,
    High,
};

// Advance to the next status in the ring: Todo → InProgress → Done → Todo.
[[nodiscard]] constexpr Status cycle_status(const Status s) noexcept
{
    switch (s)
    {
        case Status::Todo:
            return Status::InProgress;
        case Status::InProgress:
            return Status::Done;
        case Status::Done:
            return Status::Todo;
    }
    // Unreachable; present only to satisfy -Wreturn-type on some compilers.
    return Status::Todo;
}

// Advance to the next priority in the ring: Low → Medium → High → Low.
[[nodiscard]] constexpr Priority cycle_priority(const Priority p) noexcept
{
    switch (p)
    {
        case Priority::Low:
            return Priority::Medium;
        case Priority::Medium:
            return Priority::High;
        case Priority::High:
            return Priority::Low;
    }
    return Priority::Low;
}

// Per-node reference into the description string arena. A zero `length` means
// no description is currently set; `offset` is irrelevant in that case.
struct DescSlice
{
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

static_assert(sizeof(DescSlice) == 8);
static_assert(alignof(DescSlice) == 4);
static_assert(std::is_trivially_copyable_v<DescSlice>);
static_assert(std::is_standard_layout_v<DescSlice>);

// Maximum byte length accepted by set_description. Descriptions longer than
// this are rejected with Error::DescriptionTooLong.
inline constexpr std::uint32_t kMaxDescriptionBytes = 4096;

// Sentinel stored in the due_date parallel array to represent "no date set".
// INT64_MIN is chosen because it cannot be a valid Unix epoch value in any
// realistic task-manager context.
inline constexpr std::int64_t kNoDueDate = std::numeric_limits<std::int64_t>::min();

}  // namespace toe::core
