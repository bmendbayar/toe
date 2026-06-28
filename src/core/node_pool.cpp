#include <cassert>

#include "core/node_pool.hpp"

namespace toe::core
{

NodePool::NodePool(const std::uint32_t reserve_slots)
{
    generation_.reserve(reserve_slots);
    next_free_.reserve(reserve_slots);
    parent_.reserve(reserve_slots);
    first_child_.reserve(reserve_slots);
    next_sibling_.reserve(reserve_slots);
}

TaskId NodePool::allocate()
{
    ++live_count_;

    if (free_head_ != NIL)
    {
        const std::uint32_t index = free_head_;
        free_head_ = next_free_[index];
        // Recycled slots must start link-free: a reused slot carrying the
        // previous occupant's links would silently graft stale subtrees.
        parent_[index] = TaskId::invalid();
        first_child_[index] = TaskId::invalid();
        next_sibling_[index] = TaskId::invalid();
        return TaskId{.index = index, .generation = generation_[index]};
    }

    const auto index = static_cast<std::uint32_t>(generation_.size());
    generation_.push_back(1);  // live slots start at generation 1, never 0
    next_free_.push_back(NIL);
    parent_.push_back(TaskId::invalid());
    first_child_.push_back(TaskId::invalid());
    next_sibling_.push_back(TaskId::invalid());
    return TaskId{.index = index, .generation = 1};
}

std::expected<void, Error> NodePool::free(const TaskId id)
{
    if (!is_valid(id))
    {
        return std::unexpected(Error::StaleHandle);
    }

    // Bump the generation so all outstanding handles to this slot go stale.
    // Skip 0 on wraparound to preserve the "0 is never live" invariant.
    const std::uint32_t next = generation_[id.index] + 1;
    generation_[id.index] = (next == 0) ? 1 : next;

    next_free_[id.index] = free_head_;
    free_head_ = id.index;
    --live_count_;
    return {};
}

bool NodePool::is_valid(const TaskId id) const noexcept
{
    return id.index < generation_.size() && generation_[id.index] == id.generation;
}

// --- Tree construction ------------------------------------------------------

TaskId NodePool::add_root()
{
    const TaskId id = allocate();
    append_to_chain(TaskId::invalid(), id);
    return id;
}

std::expected<TaskId, Error> NodePool::add_child(const TaskId parent)
{
    if (!is_valid(parent))
    {
        return std::unexpected(Error::StaleHandle);
    }
    const TaskId id = allocate();
    append_to_chain(parent, id);
    return id;
}

std::expected<TaskId, Error> NodePool::add_sibling_after(const TaskId after)
{
    if (!is_valid(after))
    {
        return std::unexpected(Error::StaleHandle);
    }
    const TaskId id = allocate();
    insert_after(after, id);
    return id;
}

// --- Tree mutation ----------------------------------------------------------

std::expected<void, Error> NodePool::move(const TaskId id, const TaskId new_parent)
{
    if (!is_valid(id))
    {
        return std::unexpected(Error::StaleHandle);
    }

    // An invalid handle is the explicit "make it a root" request; any other
    // handle must name a live slot.
    if (new_parent.is_valid())
    {
        if (!is_valid(new_parent))
        {
            return std::unexpected(Error::StaleHandle);
        }
        // Reject attaching id to itself or to one of its own descendants: walk
        // up from new_parent and bail if we ever reach id.
        for (TaskId cur = new_parent; cur.is_valid(); cur = parent_[cur.index])
        {
            if (cur == id)
            {
                return std::unexpected(Error::WouldCreateCycle);
            }
        }
    }

    detach(id);
    append_to_chain(new_parent, id);
    return {};
}

std::expected<void, Error> NodePool::indent(const TaskId id)
{
    if (!is_valid(id))
    {
        return std::unexpected(Error::StaleHandle);
    }
    const TaskId prev = prev_sibling(id);
    if (!prev.is_valid())
    {
        return std::unexpected(Error::NoPreviousSibling);
    }
    // prev is a sibling, never a descendant of id, so this move cannot cycle.
    return move(id, prev);
}

std::expected<void, Error> NodePool::outdent(const TaskId id)
{
    if (!is_valid(id))
    {
        return std::unexpected(Error::StaleHandle);
    }
    const TaskId old_parent = parent_[id.index];
    if (!old_parent.is_valid())
    {
        return std::unexpected(Error::NoParent);
    }

    // Lift to the grandparent's level, immediately after the former parent so
    // the node lands where the user expects rather than at the chain's tail.
    detach(id);
    insert_after(old_parent, id);
    return {};
}

std::expected<void, Error> NodePool::remove_subtree(const TaskId id)
{
    if (!is_valid(id))
    {
        return std::unexpected(Error::StaleHandle);
    }

    detach(id);

    // Iterative pre-order collection of id + all descendants. Seeding the stack
    // with id's children (not id's siblings) bounds the walk to the subtree;
    // the explicit stack keeps deep nesting off the native call stack.
    std::vector<TaskId> doomed;
    doomed.push_back(id);
    std::vector<TaskId> stack;
    if (const TaskId child = first_child_[id.index]; child.is_valid())
    {
        stack.push_back(child);
    }
    while (!stack.empty())
    {
        const TaskId node = stack.back();
        stack.pop_back();
        doomed.push_back(node);
        if (const TaskId sibling = next_sibling_[node.index]; sibling.is_valid())
        {
            stack.push_back(sibling);
        }
        if (const TaskId child = first_child_[node.index]; child.is_valid())
        {
            stack.push_back(child);
        }
    }

    for (const TaskId node : doomed)
    {
        // Each handle was just gathered from live links, so free cannot fail.
        const auto freed = free(node);
        assert(freed.has_value());
        (void)freed;
    }
    return {};
}

// --- Tree inspection --------------------------------------------------------

std::expected<TaskId, Error> NodePool::parent(const TaskId id) const
{
    if (!is_valid(id))
    {
        return std::unexpected(Error::StaleHandle);
    }
    return parent_[id.index];
}

std::expected<TaskId, Error> NodePool::first_child(const TaskId id) const
{
    if (!is_valid(id))
    {
        return std::unexpected(Error::StaleHandle);
    }
    return first_child_[id.index];
}

std::expected<TaskId, Error> NodePool::next_sibling(const TaskId id) const
{
    if (!is_valid(id))
    {
        return std::unexpected(Error::StaleHandle);
    }
    return next_sibling_[id.index];
}

std::vector<TaskId> NodePool::flatten() const
{
    std::vector<TaskId> order;
    order.reserve(live_count_);

    std::vector<TaskId> stack;
    if (root_.is_valid())
    {
        stack.push_back(root_);
    }
    // Push sibling before child so the child subtree pops first: this yields
    // pre-order with sibling order preserved, without recursion.
    while (!stack.empty())
    {
        const TaskId node = stack.back();
        stack.pop_back();
        order.push_back(node);
        if (const TaskId sibling = next_sibling_[node.index]; sibling.is_valid())
        {
            stack.push_back(sibling);
        }
        if (const TaskId child = first_child_[node.index]; child.is_valid())
        {
            stack.push_back(child);
        }
    }
    return order;
}

// --- Private helpers --------------------------------------------------------

void NodePool::detach(const TaskId id)
{
    const TaskId p = parent_[id.index];
    const TaskId next = next_sibling_[id.index];

    // Repair the chain id is leaving: either fix the head (parent's first_child
    // or the forest root), or splice id out from its predecessor.
    TaskId head = p.is_valid() ? first_child_[p.index] : root_;
    if (head == id)
    {
        if (p.is_valid())
        {
            first_child_[p.index] = next;
        }
        else
        {
            root_ = next;
        }
    }
    else
    {
        TaskId cur = head;
        while (next_sibling_[cur.index] != id)
        {
            cur = next_sibling_[cur.index];
        }
        next_sibling_[cur.index] = next;
    }

    parent_[id.index] = TaskId::invalid();
    next_sibling_[id.index] = TaskId::invalid();
}

void NodePool::append_to_chain(const TaskId parent, const TaskId id)
{
    parent_[id.index] = parent;
    next_sibling_[id.index] = TaskId::invalid();

    const bool to_root = !parent.is_valid();
    const TaskId head = to_root ? root_ : first_child_[parent.index];
    if (!head.is_valid())
    {
        if (to_root)
        {
            root_ = id;
        }
        else
        {
            first_child_[parent.index] = id;
        }
        return;
    }

    TaskId tail = head;
    while (next_sibling_[tail.index].is_valid())
    {
        tail = next_sibling_[tail.index];
    }
    next_sibling_[tail.index] = id;
}

void NodePool::insert_after(const TaskId anchor, const TaskId id)
{
    parent_[id.index] = parent_[anchor.index];
    next_sibling_[id.index] = next_sibling_[anchor.index];
    next_sibling_[anchor.index] = id;
}

TaskId NodePool::prev_sibling(const TaskId id) const
{
    const TaskId p = parent_[id.index];
    const TaskId head = p.is_valid() ? first_child_[p.index] : root_;
    if (head == id)
    {
        return TaskId::invalid();
    }
    TaskId cur = head;
    while (next_sibling_[cur.index] != id)
    {
        cur = next_sibling_[cur.index];
    }
    return cur;
}

}  // namespace toe::core
