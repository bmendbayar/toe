#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "core/error.hpp"
#include "core/task_id.hpp"

namespace toe::core
{

// Structure-of-Arrays slot map (a pool with generational handles) that owns
// task slots and hands out stable TaskId handles. Unlike a bump/region arena,
// slots are freed and recycled individually through a free list.
//
// Beyond raw slot lifetime, the pool stores the task forest as a Left-Child /
// Right-Sibling (LCRS) tree: every node carries `parent`, `first_child`, and
// `next_sibling` handles in parallel arrays. Fixed-size nodes, no per-node heap
// allocation, and the whole forest is flat data addressable by index. Multiple
// top-level tasks are supported: the roots themselves form a sibling chain,
// whose head is `root_`. Sibling order is insertion order and is preserved by
// every operation.
//
// The invariant tying it together: every parallel array (`generation_`,
// `next_free_`, `parent_`, `first_child_`, `next_sibling_`) stays the same
// length as the others, indexed by TaskId::index. Later milestones add more
// payload arrays (status/priority/due-date) under the same rule.
//
// Freed slots are recycled through an intrusive LIFO free list threaded by
// indices, so freeing and reallocating cost no heap traffic. Growth is the
// only allocation, amortized by the underlying vectors. Traversal that the user
// can drive arbitrarily deep (flatten, subtree removal) is iterative with an
// explicit stack — never native recursion — so user-controlled nesting depth
// cannot overflow the call stack.
class NodePool
{
public:
    NodePool() = default;

    // Reserve slot capacity up front to keep steady-state allocation
    // heap-traffic-free (bulk-at-startup, per the pool-allocation guidance).
    explicit NodePool(std::uint32_t reserve_slots);

    // Hand out a live but *unlinked* slot (parent/child/sibling all invalid):
    // reuse a freed one if available, otherwise grow. This is the low-level
    // primitive; the tree operations below allocate and splice in one step.
    // Infallible by design: the pool grows on demand (amortized), so there
    // is no expected-error branch to leave untested. True OOM terminates.
    [[nodiscard]] TaskId allocate();

    // Release a slot back to the free list, bumping its generation so every
    // outstanding handle to it becomes detectably stale. Rejects a handle that
    // is already stale or out of range. Does not touch tree links; callers that
    // free linked nodes must unlink first (see remove_subtree).
    [[nodiscard]] std::expected<void, Error> free(TaskId id);

    // True iff `id` names a currently-live slot (index in range AND generation
    // matches the slot's current generation).
    [[nodiscard]] bool is_valid(TaskId id) const noexcept;

    // --- Tree construction --------------------------------------------------

    // Append a new top-level task to the end of the root sibling chain.
    [[nodiscard]] TaskId add_root();

    // Append a new task as the last child of `parent`, preserving the order of
    // existing children. Rejects a stale/out-of-range `parent`.
    [[nodiscard]] std::expected<TaskId, Error> add_child(TaskId parent);

    // Insert a new task immediately after `after` in its sibling chain (the
    // "open below" primitive). Works for roots and inner nodes alike.
    [[nodiscard]] std::expected<TaskId, Error> add_sibling_after(TaskId after);

    // --- Tree mutation ------------------------------------------------------

    // Reparent `id` under `new_parent`, appended as its last child. A
    // default/invalid `new_parent` moves `id` to the end of the root chain.
    // Rejects moving a node under itself or its own descendant
    // (WouldCreateCycle), and stale handles (StaleHandle).
    [[nodiscard]] std::expected<void, Error> move(TaskId id, TaskId new_parent);

    // Make `id` the last child of its immediately preceding sibling. Fails with
    // NoPreviousSibling if `id` is first among its siblings.
    [[nodiscard]] std::expected<void, Error> indent(TaskId id);

    // Lift `id` to its grandparent's level, placed immediately after its former
    // parent. Fails with NoParent if `id` is already a root.
    [[nodiscard]] std::expected<void, Error> outdent(TaskId id);

    // Detach `id` and its whole subtree from the forest and reclaim every node
    // in it. Iterative (explicit stack), so arbitrarily deep subtrees are safe.
    [[nodiscard]] std::expected<void, Error> remove_subtree(TaskId id);

    // --- Tree inspection ----------------------------------------------------

    // Head of the root sibling chain, or an invalid handle for an empty forest.
    [[nodiscard]] TaskId root() const noexcept
    {
        return root_;
    }

    [[nodiscard]] std::expected<TaskId, Error> parent(TaskId id) const;
    [[nodiscard]] std::expected<TaskId, Error> first_child(TaskId id) const;
    [[nodiscard]] std::expected<TaskId, Error> next_sibling(TaskId id) const;

    // All live nodes in depth-first pre-order across the whole forest, with
    // sibling order preserved. Iterative; safe at any depth.
    [[nodiscard]] std::vector<TaskId> flatten() const;

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

    // Remove `id` from whatever sibling chain it currently sits in (a parent's
    // child list or the root chain), repairing the head/predecessor link and
    // clearing `id`'s own parent/next-sibling. The subtree under `id` is left
    // intact. Precondition: `id` is live.
    void detach(TaskId id);

    // Splice `id` in as the last child of `parent`; an invalid `parent` appends
    // to the root chain. Precondition: `id` is detached (no stale links).
    void append_to_chain(TaskId parent, TaskId id);

    // Splice `id` in immediately after `anchor` in `anchor`'s sibling chain.
    // Precondition: `id` is detached, `anchor` is live.
    void insert_after(TaskId anchor, TaskId id);

    // The sibling immediately preceding `id`, or an invalid handle if `id` is
    // first in its chain. Precondition: `id` is live.
    [[nodiscard]] TaskId prev_sibling(TaskId id) const;

    // Current generation per slot; starts at 1 and never returns to 0, so a
    // zero-generation (default/invalid) handle never matches a live slot.
    std::vector<std::uint32_t> generation_;

    // Intrusive free list: for a freed slot, the index of the next free slot
    // (or `NIL`). Entries for live slots are unused.
    std::vector<std::uint32_t> next_free_;

    // LCRS links, one entry per slot. Invalid handle == "no such relation".
    std::vector<TaskId> parent_;
    std::vector<TaskId> first_child_;
    std::vector<TaskId> next_sibling_;

    // Head of the root sibling chain; invalid when the forest is empty.
    TaskId root_;

    std::uint32_t free_head_ = NIL;
    std::size_t live_count_ = 0;
};

}  // namespace toe::core
