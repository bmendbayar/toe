#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "core/error.hpp"
#include "core/task_id.hpp"

namespace toe::core
{

// Structure-of-Arrays arena that owns task slots and hands out stable TaskId
// handles. M1 manages only slot lifetime: allocation, freeing, and handle
// validation via the per-slot generation counter. The payload arrays (LCRS
// links, status/priority/due-date) are added as further parallel vectors in
// later milestones; the invariant is that every such array stays the same
// length as `generation_`, indexed by TaskId::index.
//
// Freed slots are recycled through an intrusive LIFO free list threaded by
// indices, so freeing and reallocating cost no heap traffic. Growth is the
// only allocation, amortized by the underlying vectors.
class NodePool
{
public:
    NodePool() = default;

    // Reserve slot capacity up front to keep steady-state allocation
    // heap-traffic-free (bulk-at-startup, per the arena guidance).
    explicit NodePool(std::uint32_t reserve_slots);

    // Hand out a live slot: reuse a freed one if available, otherwise grow.
    // Infallible by design: the arena grows on demand (amortized), so there
    // is no expected-error branch to leave untested. True OOM terminates.
    [[nodiscard]] TaskId allocate();

    // Release a slot back to the free list, bumping its generation so every
    // outstanding handle to it becomes detectably stale. Rejects a handle that
    // is already stale or out of range.
    [[nodiscard]] std::expected<void, Error> free(TaskId id);

    // True iff `id` names a currently-live slot (index in range AND generation
    // matches the slot's current generation).
    [[nodiscard]] bool is_valid(TaskId id) const noexcept;

    // Total slot count, including freed slots awaiting reuse.
    [[nodiscard]] std::size_t slot_count() const noexcept
    {
        return generation_.size();
    }

    // Number of currently-live (allocated, not freed) slots.
    [[nodiscard]] std::size_t live_count() const noexcept
    {
        return live_count_;
    }

private:
    // Sentinel terminating the free list / marking "no next free slot".
    static constexpr std::uint32_t NIL = UINT32_MAX;

    // Current generation per slot; starts at 1 and never returns to 0, so a
    // zero-generation (default/invalid) handle never matches a live slot.
    std::vector<std::uint32_t> generation_;

    // Intrusive free list: for a freed slot, the index of the next free slot
    // (or `NIL`). Entries for live slots are unused.
    std::vector<std::uint32_t> next_free_;

    std::uint32_t free_head_ = NIL;
    std::size_t live_count_ = 0;
};

}  // namespace toe::core
