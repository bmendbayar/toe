#include <gtest/gtest.h>

#include <vector>

#include "core/error.hpp"
#include "core/node_pool.hpp"
#include "core/task_id.hpp"

namespace toe::core
{

TEST(TaskId, DefaultAndInvalidAreInvalid)
{
    EXPECT_FALSE(TaskId{}.is_valid());
    EXPECT_FALSE(TaskId::invalid().is_valid());
    EXPECT_EQ(TaskId{}, TaskId::invalid());
}

TEST(NodePool, AllocateReturnsLiveHandle)
{
    NodePool pool;
    const TaskId id = pool.allocate();

    EXPECT_TRUE(id.is_valid());
    EXPECT_TRUE(pool.is_valid(id));
    EXPECT_EQ(pool.slot_count(), 1u);
    EXPECT_EQ(pool.live_count(), 1u);
}

TEST(NodePool, DefaultHandleNeverValidatesAgainstPool)
{
    NodePool pool;
    (void)pool.allocate();  // occupies slot 0 at generation 1

    // A default handle is {index: 0, generation: 0}; slot 0 is live at
    // generation 1, so the zero-generation handle must not match it.
    EXPECT_FALSE(pool.is_valid(TaskId{}));
}

TEST(NodePool, FreeReleasesSlotAndRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.allocate();

    ASSERT_TRUE(pool.free(id).has_value());
    EXPECT_EQ(pool.live_count(), 0u);
    EXPECT_EQ(pool.slot_count(), 1u);  // slot retained for reuse

    // The handle is now stale: its slot was freed (generation bumped).
    EXPECT_FALSE(pool.is_valid(id));
}

TEST(NodePool, DoubleFreeReportsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.allocate();
    ASSERT_TRUE(pool.free(id).has_value());

    const auto second = pool.free(id);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), Error::StaleHandle);
}

TEST(NodePool, FreeOutOfRangeHandleReportsStaleHandle)
{
    NodePool pool;
    const auto result = pool.free(TaskId{.index = 99, .generation = 1});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::StaleHandle);
}

TEST(NodePool, ReuseReturnsFreshNonAliasingHandle)
{
    NodePool pool;
    const TaskId first = pool.allocate();
    ASSERT_TRUE(pool.free(first).has_value());

    // Reuse must recycle the same slot index but with a bumped generation,
    // so the new handle is distinct from and does not alias the stale one.
    const TaskId second = pool.allocate();
    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.generation, first.generation);
    EXPECT_NE(second, first);

    EXPECT_TRUE(pool.is_valid(second));
    EXPECT_FALSE(pool.is_valid(first));  // stale handle stays rejected
    EXPECT_EQ(pool.slot_count(), 1u);    // no new slot allocated
}

TEST(NodePool, FreeListReusesBeforeGrowing)
{
    NodePool pool;
    const TaskId a = pool.allocate();
    const TaskId b = pool.allocate();
    const TaskId c = pool.allocate();
    ASSERT_EQ(pool.slot_count(), 3u);

    // Free out of order; LIFO free list should hand back b's slot first.
    ASSERT_TRUE(pool.free(b).has_value());
    ASSERT_TRUE(pool.free(a).has_value());

    const TaskId d = pool.allocate();
    EXPECT_EQ(d.index, a.index);  // last freed (a) is reused first
    const TaskId e = pool.allocate();
    EXPECT_EQ(e.index, b.index);

    EXPECT_EQ(pool.slot_count(), 3u);  // still no growth: reused freed slots
    EXPECT_TRUE(pool.is_valid(c));
    EXPECT_EQ(pool.live_count(), 3u);
}

TEST(NodePool, ManyHandlesRemainIndependentlyValid)
{
    NodePool pool{128};
    std::vector<TaskId> ids;
    ids.reserve(100);
    for (int i = 0; i < 100; ++i)
    {
        ids.push_back(pool.allocate());
    }

    EXPECT_EQ(pool.live_count(), 100u);
    for (const TaskId id : ids)
    {
        EXPECT_TRUE(pool.is_valid(id));
    }

    // Free every other handle; the rest must stay valid and the freed ones
    // must all be rejected.
    for (std::size_t i = 0; i < ids.size(); i += 2)
    {
        ASSERT_TRUE(pool.free(ids[i]).has_value());
    }
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        EXPECT_EQ(pool.is_valid(ids[i]), (i % 2 == 1));
    }
    EXPECT_EQ(pool.live_count(), 50u);
}

}  // namespace toe::core
