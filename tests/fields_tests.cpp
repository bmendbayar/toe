#include <gtest/gtest.h>

#include <string>

#include "core/error.hpp"
#include "core/fields.hpp"
#include "core/node_pool.hpp"

namespace toe::core
{

TEST(Fields, CycleStatusWrapsRing)
{
    EXPECT_EQ(cycle_status(Status::TODO), Status::IN_PROGRESS);
    EXPECT_EQ(cycle_status(Status::IN_PROGRESS), Status::DONE);
    EXPECT_EQ(cycle_status(Status::DONE), Status::TODO);
}

TEST(Fields, CyclePriorityWrapsRing)
{
    EXPECT_EQ(cycle_priority(Priority::LOW), Priority::MEDIUM);
    EXPECT_EQ(cycle_priority(Priority::MEDIUM), Priority::HIGH);
    EXPECT_EQ(cycle_priority(Priority::HIGH), Priority::LOW);
}

TEST(Fields, DefaultStatusIsTodo)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.get_status(id).has_value());
    EXPECT_EQ(pool.get_status(id).value(), Status::TODO);
}

TEST(Fields, DefaultPriorityIsLow)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.get_priority(id).has_value());
    EXPECT_EQ(pool.get_priority(id).value(), Priority::LOW);
}

TEST(Fields, DefaultDueDateIsAbsent)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    const auto result = pool.get_due_date(id);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value().has_value());
}

TEST(Fields, DefaultDescriptionIsEmpty)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    const auto result = pool.get_description(id);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty());
}

TEST(Fields, SetAndGetStatus)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    ASSERT_TRUE(pool.set_status(id, Status::IN_PROGRESS).has_value());
    EXPECT_EQ(pool.get_status(id).value(), Status::IN_PROGRESS);

    ASSERT_TRUE(pool.set_status(id, Status::DONE).has_value());
    EXPECT_EQ(pool.get_status(id).value(), Status::DONE);
}

TEST(Fields, SetAndGetPriority)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    ASSERT_TRUE(pool.set_priority(id, Priority::HIGH).has_value());
    EXPECT_EQ(pool.get_priority(id).value(), Priority::HIGH);

    ASSERT_TRUE(pool.set_priority(id, Priority::MEDIUM).has_value());
    EXPECT_EQ(pool.get_priority(id).value(), Priority::MEDIUM);
}

TEST(Fields, SetAndGetDueDate)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    constexpr std::int64_t kDate = 1'750'000'000LL;

    ASSERT_TRUE(pool.set_due_date(id, kDate).has_value());
    const auto result = pool.get_due_date(id);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result.value().has_value());
    EXPECT_EQ(result.value().value(), kDate);
}

TEST(Fields, ClearDueDateReturnsAbsent)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    ASSERT_TRUE(pool.set_due_date(id, 1'000'000LL).has_value());
    ASSERT_TRUE(pool.set_due_date(id, std::nullopt).has_value());

    const auto result = pool.get_due_date(id);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value().has_value());
}

TEST(Fields, SetAndGetDescription)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    ASSERT_TRUE(pool.set_description(id, "buy groceries").has_value());
    const auto result = pool.get_description(id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "buy groceries");
}

TEST(Fields, AdvanceStatusCyclesThroughAllValues)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    // Start at Todo (default).
    auto r = pool.advance_status(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Status::IN_PROGRESS);
    EXPECT_EQ(pool.get_status(id).value(), Status::IN_PROGRESS);

    r = pool.advance_status(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Status::DONE);

    r = pool.advance_status(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Status::TODO);  // wrapped
}

TEST(Fields, AdvancePriorityCyclesThroughAllValues)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    // Start at Low (default).
    auto r = pool.advance_priority(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Priority::MEDIUM);
    EXPECT_EQ(pool.get_priority(id).value(), Priority::MEDIUM);

    r = pool.advance_priority(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Priority::HIGH);

    r = pool.advance_priority(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), Priority::LOW);  // wrapped
}

TEST(Fields, SetDescriptionOverwritesPreviousValue)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    ASSERT_TRUE(pool.set_description(id, "first").has_value());
    ASSERT_TRUE(pool.set_description(id, "second").has_value());

    // Only the latest value is visible; old arena bytes are orphaned.
    EXPECT_EQ(pool.get_description(id).value(), "second");
}

TEST(Fields, SetDescriptionOnMultipleNodesAreIndependent)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();

    ASSERT_TRUE(pool.set_description(a, "alpha").has_value());
    ASSERT_TRUE(pool.set_description(b, "beta").has_value());

    EXPECT_EQ(pool.get_description(a).value(), "alpha");
    EXPECT_EQ(pool.get_description(b).value(), "beta");
}

TEST(Fields, ManyDescriptionEditsGrowArenaWithoutAliasing)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    for (int i = 0; i < 500; ++i)
    {
        const std::string s = "task " + std::to_string(i);
        ASSERT_TRUE(pool.set_description(id, s).has_value());
        EXPECT_EQ(pool.get_description(id).value(), s);
    }
}

TEST(Fields, SetDescriptionRejectsEmpty)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    const auto result = pool.set_description(id, "");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::EMPTY_DESCRIPTION);
}

TEST(Fields, SetDescriptionRejectsTooLong)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    const std::string too_long(MAX_DESCRIPTION_BYTES + 1, 'x');
    const auto result = pool.set_description(id, too_long);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::DESCRIPTION_TOO_LONG);
}

TEST(Fields, SetDescriptionAtExactLimitSucceeds)
{
    NodePool pool;
    const TaskId id = pool.add_root();

    const std::string at_limit(MAX_DESCRIPTION_BYTES, 'y');
    EXPECT_TRUE(pool.set_description(id, at_limit).has_value());
    EXPECT_EQ(pool.get_description(id).value().size(), MAX_DESCRIPTION_BYTES);
}

TEST(Fields, GetStatusRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.get_status(id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, SetStatusRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.set_status(id, Status::DONE);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, AdvanceStatusRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.advance_status(id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, GetPriorityRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.get_priority(id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, SetPriorityRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.set_priority(id, Priority::HIGH);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, AdvancePriorityRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.advance_priority(id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, GetDueDateRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.get_due_date(id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, SetDueDateRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.set_due_date(id, 0LL);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, GetDescriptionRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.get_description(id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

TEST(Fields, SetDescriptionRejectsStaleHandle)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(id).has_value());

    const auto result = pool.set_description(id, "hello");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::STALE_HANDLE);
}

// When a freed slot is reused, its fields must be reset to defaults, not
// expose the previous occupant's values.
TEST(Fields, FreedSlotFieldsResetOnReuse)
{
    NodePool pool;
    const TaskId first = pool.add_root();

    ASSERT_TRUE(pool.set_status(first, Status::DONE).has_value());
    ASSERT_TRUE(pool.set_priority(first, Priority::HIGH).has_value());
    ASSERT_TRUE(pool.set_due_date(first, 9'999'999LL).has_value());
    ASSERT_TRUE(pool.set_description(first, "stale data").has_value());

    ASSERT_TRUE(pool.remove_subtree(first).has_value());

    // The recycled slot must not expose the previous occupant's fields.
    const TaskId reused = pool.add_root();
    EXPECT_EQ(pool.get_status(reused).value(), Status::TODO);
    EXPECT_EQ(pool.get_priority(reused).value(), Priority::LOW);
    EXPECT_FALSE(pool.get_due_date(reused).value().has_value());
    EXPECT_TRUE(pool.get_description(reused).value().empty());
}

}  // namespace toe::core
