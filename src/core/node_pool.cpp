#include "core/node_pool.hpp"

namespace toe::core
{

NodePool::NodePool(const std::uint32_t reserve_slots)
{
    generation_.reserve(reserve_slots);
    next_free_.reserve(reserve_slots);
}

TaskId NodePool::allocate()
{
    ++live_count_;

    if (free_head_ != NIL)
    {
        const std::uint32_t index = free_head_;
        free_head_ = next_free_[index];
        return TaskId{.index = index, .generation = generation_[index]};
    }

    const auto index = static_cast<std::uint32_t>(generation_.size());
    generation_.push_back(1);  // live slots start at generation 1, never 0
    next_free_.push_back(NIL);
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

}  // namespace toe::core
