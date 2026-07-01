#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/fields.hpp"
#include "core/node_pool.hpp"
#include "core/serialization.hpp"

namespace toe::core
{
namespace
{

// Header field byte offsets, mirrored from serialization.cpp's layout so the
// corruption tests can poke individual fields without re-deriving the format.
constexpr std::size_t kOffMagic = 0;
constexpr std::size_t kOffVersion = 4;
constexpr std::size_t kOffFreeHead = 12;
constexpr std::size_t kOffArrays = 40;  // first parallel array begins here

void poke_u32(std::vector<std::byte> &bytes, const std::size_t off, const std::uint32_t v)
{
    std::memcpy(bytes.data() + off, &v, sizeof(v));
}

// Compare two pools at the behavioral level: identical slot/live counts, the
// same flattened handle sequence (a faithful snapshot preserves indices and
// generations), and matching fields and links on every live node.
void expect_equivalent(const NodePool &a, const NodePool &b)
{
    ASSERT_EQ(a.slot_count(), b.slot_count());
    ASSERT_EQ(a.live_count(), b.live_count());
    EXPECT_EQ(a.root(), b.root());

    const std::vector<TaskId> fa = a.flatten();
    const std::vector<TaskId> fb = b.flatten();
    ASSERT_EQ(fa.size(), fb.size());
    for (std::size_t i = 0; i < fa.size(); ++i)
    {
        const TaskId id = fa[i];
        EXPECT_EQ(id, fb[i]);
        EXPECT_EQ(a.get_status(id).value(), b.get_status(id).value());
        EXPECT_EQ(a.get_priority(id).value(), b.get_priority(id).value());
        EXPECT_EQ(a.get_due_date(id).value(), b.get_due_date(id).value());
        EXPECT_EQ(a.get_description(id).value(), b.get_description(id).value());
        EXPECT_EQ(a.parent(id).value(), b.parent(id).value());
        EXPECT_EQ(a.first_child(id).value(), b.first_child(id).value());
        EXPECT_EQ(a.next_sibling(id).value(), b.next_sibling(id).value());
    }
}

// Round-trip `pool` through the in-memory codec and assert equivalence.
void expect_round_trips(const NodePool &pool)
{
    const std::vector<std::byte> bytes = serialize(pool);
    const auto loaded = deserialize(bytes);
    ASSERT_TRUE(loaded.has_value());
    expect_equivalent(pool, loaded.value());
}

// --- Round trips ------------------------------------------------------------

TEST(Serialization, RoundTripEmptyForest)
{
    const NodePool pool;
    const std::vector<std::byte> bytes = serialize(pool);
    const auto loaded = deserialize(bytes);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->slot_count(), 0u);
    EXPECT_EQ(loaded->live_count(), 0u);
    EXPECT_FALSE(loaded->root().is_valid());
}

TEST(Serialization, RoundTripSingleNode)
{
    NodePool pool;
    (void)pool.add_root();
    expect_round_trips(pool);
}

TEST(Serialization, RoundTripPreservesFields)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_child(a).value();
    const TaskId c = pool.add_child(a).value();

    ASSERT_TRUE(pool.set_status(a, Status::IN_PROGRESS).has_value());
    ASSERT_TRUE(pool.set_priority(b, Priority::HIGH).has_value());
    ASSERT_TRUE(pool.set_due_date(c, 1'700'000'000).has_value());
    ASSERT_TRUE(pool.set_description(a, "root task").has_value());
    ASSERT_TRUE(pool.set_description(b, "child with a longer description").has_value());

    expect_round_trips(pool);

    // Spot-check the decoded values, not just structural equivalence.
    const auto loaded = deserialize(serialize(pool));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->get_status(a).value(), Status::IN_PROGRESS);
    EXPECT_EQ(loaded->get_priority(b).value(), Priority::HIGH);
    EXPECT_EQ(loaded->get_due_date(c).value(), std::optional<std::int64_t>{1'700'000'000});
    EXPECT_EQ(loaded->get_description(a).value(), std::string_view{"root task"});
}

TEST(Serialization, RoundTripPreservesMultiRootForest)
{
    NodePool pool;
    const TaskId r0 = pool.add_root();
    (void)pool.add_root();
    const TaskId r2 = pool.add_root();
    pool.add_child(r0).value();
    pool.add_child(r2).value();
    expect_round_trips(pool);
}

TEST(Serialization, RoundTripAfterDeletionPreservesFreeList)
{
    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_child(a).value();
    pool.add_child(b).value();
    pool.add_child(a).value();

    // Free a subtree so the pool carries a non-trivial free list and a hole.
    ASSERT_TRUE(pool.remove_subtree(b).has_value());
    const std::size_t slots_before = pool.slot_count();
    const std::size_t live_before = pool.live_count();

    const auto loaded = deserialize(serialize(pool));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->slot_count(), slots_before);  // freed slots are preserved
    EXPECT_EQ(loaded->live_count(), live_before);
    expect_equivalent(pool, loaded.value());
}

TEST(Serialization, CompactsOrphanedDescriptionBytes)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    // Re-editing orphans the first description's bytes in the live arena.
    ASSERT_TRUE(pool.set_description(id, "the original, longer description").has_value());
    ASSERT_TRUE(pool.set_description(id, "short").has_value());

    const std::vector<std::byte> bytes = serialize(pool);
    // One slot: arena follows 40-byte header + 50 bytes of slot arrays. The
    // compacted arena must hold only the surviving 5-byte description.
    ASSERT_EQ(bytes.size(), 40u + 50u + 5u);

    const auto loaded = deserialize(bytes);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->get_description(id).value(), std::string_view{"short"});
}

TEST(Serialization, RoundTripDeepTreeNoStackOverflow)
{
    NodePool pool;
    TaskId cur = pool.add_root();
    constexpr int kDepth = 5000;
    for (int i = 0; i < kDepth; ++i)
    {
        cur = pool.add_child(cur).value();
    }
    ASSERT_TRUE(pool.set_description(cur, "deepest").has_value());

    const auto loaded = deserialize(serialize(pool));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->live_count(), static_cast<std::size_t>(kDepth) + 1);
    EXPECT_EQ(loaded->flatten().size(), static_cast<std::size_t>(kDepth) + 1);
    expect_equivalent(pool, loaded.value());
}

// --- Failure branches -------------------------------------------------------

TEST(Serialization, RejectsBadMagic)
{
    NodePool pool;
    (void)pool.add_root();
    std::vector<std::byte> bytes = serialize(pool);
    bytes[kOffMagic] = std::byte{'X'};

    const auto loaded = deserialize(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::BAD_MAGIC);
}

TEST(Serialization, RejectsUnsupportedVersion)
{
    NodePool pool;
    (void)pool.add_root();
    std::vector<std::byte> bytes = serialize(pool);
    poke_u32(bytes, kOffVersion, 999);

    const auto loaded = deserialize(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::UNSUPPORTED_VERSION);
}

TEST(Serialization, RejectsTruncatedHeader)
{
    NodePool pool;
    (void)pool.add_root();
    const std::vector<std::byte> bytes = serialize(pool);
    const std::span<const std::byte> cut{bytes.data(), 10};  // mid-header

    const auto loaded = deserialize(cut);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::TRUNCATED_DATA);
}

TEST(Serialization, RejectsTruncatedBody)
{
    NodePool pool;
    const TaskId id = pool.add_root();
    ASSERT_TRUE(pool.set_description(id, "payload").has_value());
    std::vector<std::byte> bytes = serialize(pool);
    bytes.pop_back();  // drop the last arena byte the header still promises

    const auto loaded = deserialize(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::TRUNCATED_DATA);
}

TEST(Serialization, RejectsTrailingGarbage)
{
    NodePool pool;
    (void)pool.add_root();
    std::vector<std::byte> bytes = serialize(pool);
    bytes.push_back(std::byte{0});

    const auto loaded = deserialize(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::CORRUPT_DATA);
}

TEST(Serialization, RejectsOutOfRangeFreeHead)
{
    NodePool pool;
    (void)pool.add_root();
    (void)pool.add_root();  // slot_count == 2, free_head == NIL
    std::vector<std::byte> bytes = serialize(pool);
    poke_u32(bytes, kOffFreeHead, 2);  // index past the last slot

    const auto loaded = deserialize(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::CORRUPT_DATA);
}

TEST(Serialization, RejectsCorruptHandle)
{
    NodePool pool;
    const TaskId a = pool.add_root();  // slot 0
    pool.add_child(a).value();         // slot 1, parent -> {0,1}
    std::vector<std::byte> bytes = serialize(pool);

    // parent array begins at kOffArrays + 8*slot_count; slot 1's entry is the
    // second TaskId, whose generation word sits 4 bytes in.
    constexpr std::size_t slot_count = 2;
    const std::size_t parent_base = kOffArrays + (8 * slot_count);
    const std::size_t child_parent_gen = parent_base + (1 * 8) + 4;
    poke_u32(bytes, child_parent_gen, 77);  // generation no live slot carries

    const auto loaded = deserialize(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::CORRUPT_DATA);
}

TEST(Serialization, RejectsCorruptDescriptionOffset)
{
    NodePool pool;
    const TaskId a = pool.add_root();            // slot 0
    const TaskId b = pool.add_child(a).value();  // slot 1
    ASSERT_TRUE(pool.set_description(b, "x").has_value());
    std::vector<std::byte> bytes = serialize(pool);

    // desc array begins at kOffArrays + 42*slot_count; slot 1's offset word is
    // the first 4 bytes of its DescSlice.
    constexpr std::size_t slot_count = 2;
    const std::size_t desc_base = kOffArrays + (42 * slot_count);
    const std::size_t child_desc_offset = desc_base + (1 * 8);
    poke_u32(bytes, child_desc_offset, 0xFFFF'FFF0);  // points past the arena

    const auto loaded = deserialize(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::CORRUPT_DATA);
}

// --- Filesystem -------------------------------------------------------------

class ScopedXdg
{
public:
    explicit ScopedXdg(const std::filesystem::path &dir)
    {
        if (const char *prev = std::getenv("XDG_CONFIG_HOME"))
        {
            had_prev_ = true;
            prev_ = prev;
        }
        ::setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
    }
    ScopedXdg(const ScopedXdg &) = delete;
    ScopedXdg &operator=(const ScopedXdg &) = delete;
    ~ScopedXdg()
    {
        if (had_prev_)
        {
            ::setenv("XDG_CONFIG_HOME", prev_.c_str(), 1);
        }
        else
        {
            ::unsetenv("XDG_CONFIG_HOME");
        }
    }

private:
    bool had_prev_ = false;
    std::string prev_;
};

TEST(Serialization, TasksPathHonorsXdgConfigHome)
{
    const ScopedXdg guard{"/tmp/toe-xdg-test"};
    EXPECT_EQ(tasks_path(), std::filesystem::path{"/tmp/toe-xdg-test/toe/tasks.bin"});
}

TEST(Serialization, SaveThenLoadRoundTripsThroughFile)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "toe-save-load-test";
    fs::remove_all(dir);
    const ScopedXdg guard{dir};

    NodePool pool;
    const TaskId a = pool.add_root();
    const TaskId b = pool.add_child(a).value();
    ASSERT_TRUE(pool.set_description(b, "persisted").has_value());

    const fs::path path = tasks_path();
    ASSERT_TRUE(save(pool, path).has_value());
    ASSERT_TRUE(fs::exists(path));

    const auto loaded = load(path);
    ASSERT_TRUE(loaded.has_value());
    expect_equivalent(pool, loaded.value());

    fs::remove_all(dir);
}

TEST(Serialization, LoadMissingFileReportsIoError)
{
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "toe-definitely-absent.bin";
    fs::remove(path);

    const auto loaded = load(path);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Error::IO_ERROR);
}

}  // namespace
}  // namespace toe::core
