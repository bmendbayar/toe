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

// --- LCRS tree ----------------------------------------------------------

// Children of `parent` in sibling order; an invalid `parent` walks the roots.
std::vector<TaskId> children_of(const NodePool &pool, const TaskId parent)
{
    std::vector<TaskId> out;
    TaskId cur = parent.is_valid() ? pool.first_child(parent).value() : pool.root();
    for (; cur.is_valid(); cur = pool.next_sibling(cur).value())
    {
        out.push_back(cur);
    }
    return out;
}

TEST(Tree, EmptyForestHasNoRoot)
{
    const NodePool pool;
    EXPECT_FALSE(pool.root().is_valid());
    EXPECT_TRUE(pool.flatten().empty());
}

TEST(Tree, AddRootBecomesRootWithNoLinks)
{
    NodePool pool;
    const TaskId r = pool.add_root();

    EXPECT_EQ(pool.root(), r);
    EXPECT_FALSE(pool.parent(r).value().is_valid());
    EXPECT_FALSE(pool.first_child(r).value().is_valid());
    EXPECT_FALSE(pool.next_sibling(r).value().is_valid());
    EXPECT_EQ(pool.flatten(), std::vector<TaskId>{r});
}

TEST(Tree, RootsFormSiblingChainInInsertionOrder)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    const TaskId c = pool.add_root();

    EXPECT_EQ(pool.root(), a);
    EXPECT_EQ(children_of(pool, TaskId::invalid()), (std::vector<TaskId>{a, b, c}));
    for (const TaskId r : {a, b, c})
    {
        EXPECT_FALSE(pool.parent(r).value().is_valid());
    }
}

TEST(Tree, AddChildAppendsPreservingOrder)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId c0 = pool.add_child(root).value();
    const TaskId c1 = pool.add_child(root).value();
    const TaskId c2 = pool.add_child(root).value();

    EXPECT_EQ(pool.first_child(root).value(), c0);
    EXPECT_EQ(children_of(pool, root), (std::vector<TaskId>{c0, c1, c2}));
    for (const TaskId c : {c0, c1, c2})
    {
        EXPECT_EQ(pool.parent(c).value(), root);
    }
}

TEST(Tree, AddChildRejectsStaleParent)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(root).has_value());

    const auto result = pool.add_child(root);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::StaleHandle);
}

TEST(Tree, AddSiblingAfterInsertsInPlace)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId a = pool.add_child(root).value();
    const TaskId c = pool.add_child(root).value();

    // Insert b between a and c rather than at the tail.
    const TaskId b = pool.add_sibling_after(a).value();

    EXPECT_EQ(children_of(pool, root), (std::vector<TaskId>{a, b, c}));
    EXPECT_EQ(pool.parent(b).value(), root);
}

TEST(Tree, AddSiblingAfterRootExtendsRootChain)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_sibling_after(a).value();

    EXPECT_EQ(pool.root(), a);
    EXPECT_FALSE(pool.parent(b).value().is_valid());
    EXPECT_EQ(children_of(pool, TaskId::invalid()), (std::vector<TaskId>{a, b}));
}

TEST(Tree, FlattenIsDepthFirstPreOrder)
{
    // root: A(A1, A2), B  -> A, A1, A2, B
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    const TaskId a1 = pool.add_child(a).value();
    const TaskId a2 = pool.add_child(a).value();

    EXPECT_EQ(pool.flatten(), (std::vector<TaskId>{a, a1, a2, b}));
}

TEST(Tree, MoveReparentsAsLastChild)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    const TaskId b_child = pool.add_child(b).value();

    ASSERT_TRUE(pool.move(a, b).has_value());

    EXPECT_EQ(pool.root(), b);  // a is no longer a root
    EXPECT_EQ(pool.parent(a).value(), b);
    EXPECT_EQ(children_of(pool, b), (std::vector<TaskId>{b_child, a}));
}

TEST(Tree, MoveToInvalidParentMakesItARoot)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId child = pool.add_child(root).value();

    ASSERT_TRUE(pool.move(child, TaskId::invalid()).has_value());

    EXPECT_FALSE(pool.parent(child).value().is_valid());
    EXPECT_FALSE(pool.first_child(root).value().is_valid());
    EXPECT_EQ(children_of(pool, TaskId::invalid()), (std::vector<TaskId>{root, child}));
}

TEST(Tree, MoveUnderSelfIsRejected)
{
    NodePool pool;
    const TaskId a = pool.add_root();

    const auto result = pool.move(a, a);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::WouldCreateCycle);
}

TEST(Tree, MoveUnderOwnDescendantIsRejected)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_child(a).value();
    const TaskId c = pool.add_child(b).value();

    // a -> b -> c; moving a under c would splice a cycle.
    const auto result = pool.move(a, c);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::WouldCreateCycle);

    // The forest must be untouched by the rejected move.
    EXPECT_EQ(pool.flatten(), (std::vector<TaskId>{a, b, c}));
}

TEST(Tree, MoveRejectsStaleHandles)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(b).has_value());

    EXPECT_EQ(pool.move(b, a).error(), Error::StaleHandle);
    EXPECT_EQ(pool.move(a, b).error(), Error::StaleHandle);
}

TEST(Tree, IndentMakesNodeChildOfPreviousSibling)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId a = pool.add_child(root).value();
    const TaskId b = pool.add_child(root).value();

    ASSERT_TRUE(pool.indent(b).has_value());

    EXPECT_EQ(pool.parent(b).value(), a);
    EXPECT_EQ(children_of(pool, root), (std::vector<TaskId>{a}));
    EXPECT_EQ(children_of(pool, a), (std::vector<TaskId>{b}));
}

TEST(Tree, IndentFirstSiblingIsRejected)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId a = pool.add_child(root).value();

    const auto result = pool.indent(a);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::NoPreviousSibling);
}

TEST(Tree, OutdentLiftsAfterFormerParent)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId p = pool.add_child(root).value();
    const TaskId q = pool.add_child(root).value();  // p's following sibling
    const TaskId child = pool.add_child(p).value();

    ASSERT_TRUE(pool.outdent(child).has_value());

    // child rises to root's children, placed immediately after p (before q).
    EXPECT_EQ(pool.parent(child).value(), root);
    EXPECT_EQ(children_of(pool, root), (std::vector<TaskId>{p, child, q}));
    EXPECT_FALSE(pool.first_child(p).value().is_valid());
}

TEST(Tree, OutdentToRootLevel)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId child = pool.add_child(root).value();

    ASSERT_TRUE(pool.outdent(child).has_value());

    EXPECT_FALSE(pool.parent(child).value().is_valid());
    EXPECT_EQ(children_of(pool, TaskId::invalid()), (std::vector<TaskId>{root, child}));
}

TEST(Tree, OutdentRootIsRejected)
{
    NodePool pool;
    const TaskId root = pool.add_root();

    const auto result = pool.outdent(root);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::NoParent);
}

TEST(Tree, RemoveSingleNodeEmptiesForest)
{
    NodePool pool;
    const TaskId root = pool.add_root();

    ASSERT_TRUE(pool.remove_subtree(root).has_value());
    EXPECT_FALSE(pool.root().is_valid());
    EXPECT_EQ(pool.live_count(), 0u);
    EXPECT_FALSE(pool.is_valid(root));
}

TEST(Tree, RemoveSubtreeReclaimsAllDescendants)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId keep = pool.add_child(root).value();
    const TaskId drop = pool.add_child(root).value();
    const TaskId d0 = pool.add_child(drop).value();
    const TaskId d1 = pool.add_child(drop).value();
    const TaskId d00 = pool.add_child(d0).value();

    ASSERT_TRUE(pool.remove_subtree(drop).has_value());

    // The whole drop subtree is gone; every handle into it is now stale.
    for (const TaskId gone : {drop, d0, d1, d00})
    {
        EXPECT_FALSE(pool.is_valid(gone));
    }
    // The rest of the forest is intact and unlinked from the removed subtree.
    EXPECT_TRUE(pool.is_valid(root));
    EXPECT_TRUE(pool.is_valid(keep));
    EXPECT_EQ(children_of(pool, root), (std::vector<TaskId>{keep}));
    EXPECT_EQ(pool.flatten(), (std::vector<TaskId>{root, keep}));
    EXPECT_EQ(pool.live_count(), 2u);
}

TEST(Tree, RemoveSubtreeRejectsStaleHandle)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    ASSERT_TRUE(pool.remove_subtree(root).has_value());

    const auto result = pool.remove_subtree(root);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::StaleHandle);
}

TEST(Tree, RemovedSlotsAreRecycledWithoutAliasing)
{
    NodePool pool;
    const TaskId root = pool.add_root();
    const TaskId child = pool.add_child(root).value();
    ASSERT_TRUE(pool.remove_subtree(root).has_value());
    ASSERT_EQ(pool.live_count(), 0u);

    // Reuse must hand back the same slots at a bumped generation; the old
    // handles stay stale rather than aliasing the new nodes.
    const TaskId reused = pool.add_root();
    EXPECT_FALSE(pool.is_valid(root));
    EXPECT_FALSE(pool.is_valid(child));
    EXPECT_TRUE(pool.is_valid(reused));
    EXPECT_TRUE(reused == root || reused == child || reused.index == root.index || reused.index == child.index);
    EXPECT_NE(reused, root);
    EXPECT_NE(reused, child);
}

// The whole point of the iterative design: user-controlled depth must not
// touch the native call stack. A recursive implementation overflows here.
TEST(Tree, DeepNestingTraversesAndDeletesWithoutStackOverflow)
{
    constexpr int kDepth = 200'000;
    NodePool pool{kDepth + 1};

    const TaskId root = pool.add_root();
    TaskId cur = root;
    for (int i = 0; i < kDepth; ++i)
    {
        cur = pool.add_child(cur).value();
    }
    EXPECT_EQ(pool.live_count(), static_cast<std::size_t>(kDepth) + 1);

    // Iterative flatten must visit every level.
    const std::vector<TaskId> order = pool.flatten();
    EXPECT_EQ(order.size(), static_cast<std::size_t>(kDepth) + 1);
    EXPECT_EQ(order.front(), root);

    // Iterative removal must reclaim the whole chain, no recursion.
    ASSERT_TRUE(pool.remove_subtree(root).has_value());
    EXPECT_EQ(pool.live_count(), 0u);
    EXPECT_FALSE(pool.root().is_valid());
}

}  // namespace toe::core
