#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <type_traits>

#include "core/fields.hpp"
#include "core/serialization.hpp"
#include "core/task_id.hpp"

namespace toe::core
{

// The format blits native multi-byte values straight to disk; a big-endian host
// would silently write an incompatible file. Fail the build there rather than
// produce something that round-trips only on the machine that wrote it.
static_assert(std::endian::native == std::endian::little, "serialization assumes a little-endian host");

namespace
{

// Format constants. The magic doubles as a version tag's neighbour: bump
// kVersion (and add a migration path) whenever the layout below changes.
constexpr char MAGIC[4] = {'T', 'O', 'E', '1'};
constexpr std::uint32_t VERSION = 1;

// Header: magic(4) + version(4) + slot_count(4) + free_head(4) + root_index(4)
// + root_generation(4) + live_count(8) + arena_size(8).
constexpr std::size_t HEADER_BYTES = 40;

// Per-slot on-disk footprint, summed across every parallel array:
// generation(4) + next_free(4) + parent(8) + first_child(8) + next_sibling(8)
// + status(1) + priority(1) + due_date(8) + desc(8).
constexpr std::size_t PER_SLOT_BYTES = 50;

// --- Writing ----------------------------------------------------------------

void append_bytes(std::vector<std::byte> &out, const void *src, std::size_t n)
{
    const auto *p = static_cast<const std::byte *>(src);
    out.insert(out.end(), p, p + n);
}

void put_u32(std::vector<std::byte> &out, const std::uint32_t v)
{
    append_bytes(out, &v, sizeof(v));
}

void put_u64(std::vector<std::byte> &out, const std::uint64_t v)
{
    append_bytes(out, &v, sizeof(v));
}

template <typename T>
void put_array(std::vector<std::byte> &out, const std::vector<T> &v)
{
    static_assert(std::is_trivially_copyable_v<T>);
    append_bytes(out, v.data(), v.size() * sizeof(T));
}

// --- Reading ----------------------------------------------------------------

// Cursor over the input blob. Every read bounds-checks first, so a buffer that
// ends mid-field yields TruncatedData instead of reading past the end.
class Reader
{
public:
    explicit Reader(const std::span<const std::byte> data)
        : data_{data}
    {
    }

    [[nodiscard]] std::expected<void, Error> read_raw(void *dst, const std::size_t n)
    {
        if (n > data_.size() - cursor_)
        {
            return std::unexpected(Error::TRUNCATED_DATA);
        }
        std::memcpy(dst, data_.data() + cursor_, n);
        cursor_ += n;
        return {};
    }

    [[nodiscard]] std::expected<void, Error> read_u32(std::uint32_t &v)
    {
        return read_raw(&v, sizeof(v));
    }

    [[nodiscard]] std::expected<void, Error> read_u64(std::uint64_t &v)
    {
        return read_raw(&v, sizeof(v));
    }

    template <typename T>
    [[nodiscard]] std::expected<void, Error> read_array(std::vector<T> &out, const std::size_t count)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        // Divide before multiplying so a corrupt, oversized count cannot overflow
        // into a small product that would pass a naive bounds check.
        if (count > (data_.size() - cursor_) / sizeof(T))
        {
            return std::unexpected(Error::TRUNCATED_DATA);
        }
        out.resize(count);
        if (count != 0)
        {
            std::memcpy(out.data(), data_.data() + cursor_, count * sizeof(T));
            cursor_ += count * sizeof(T);
        }
        return {};
    }

    [[nodiscard]] bool at_end() const noexcept
    {
        return cursor_ == data_.size();
    }

private:
    std::span<const std::byte> data_;
    std::size_t cursor_ = 0;
};

}  // namespace

std::vector<std::byte> serialize(const NodePool &pool)
{
    const auto slot_count = static_cast<std::uint32_t>(pool.generation_.size());

    // Mark freed slots by walking the free list; their links and descriptions
    // are stale garbage and must not leak into the file.
    std::vector<bool> is_free(slot_count, false);
    for (std::uint32_t i = pool.free_head_; i != NodePool::NIL; i = pool.next_free_[i])
    {
        is_free[i] = true;
    }

    // Build the compacted views: copy each live node's description bytes into a
    // fresh arena (rewriting its offset), and blank out every freed slot so the
    // reader's live-slot validation never trips over leftover links.
    std::vector<char> arena;
    std::vector<DescSlice> desc(slot_count);
    std::vector<TaskId> parent(slot_count);
    std::vector<TaskId> first_child(slot_count);
    std::vector<TaskId> next_sibling(slot_count);
    for (std::uint32_t i = 0; i < slot_count; ++i)
    {
        if (is_free[i])
        {
            continue;  // links/desc stay default-invalid for freed slots
        }
        parent[i] = pool.parent_[i];
        first_child[i] = pool.first_child_[i];
        next_sibling[i] = pool.next_sibling_[i];

        const DescSlice &sl = pool.desc_[i];
        if (sl.length == 0)
        {
            continue;
        }
        const auto offset = static_cast<std::uint32_t>(arena.size());
        const auto *begin = pool.desc_arena_.data() + sl.offset;
        arena.insert(arena.end(), begin, begin + sl.length);
        desc[i] = DescSlice{.offset = offset, .length = sl.length};
    }

    std::vector<std::byte> out;
    out.reserve(HEADER_BYTES + (static_cast<std::size_t>(slot_count) * PER_SLOT_BYTES) + arena.size());

    append_bytes(out, MAGIC, sizeof(MAGIC));
    put_u32(out, VERSION);
    put_u32(out, slot_count);
    put_u32(out, pool.free_head_);
    put_u32(out, pool.root_.index);
    put_u32(out, pool.root_.generation);
    put_u64(out, static_cast<std::uint64_t>(pool.live_count_));
    put_u64(out, static_cast<std::uint64_t>(arena.size()));

    put_array(out, pool.generation_);
    put_array(out, pool.next_free_);
    put_array(out, parent);
    put_array(out, first_child);
    put_array(out, next_sibling);
    put_array(out, pool.status_);
    put_array(out, pool.priority_);
    put_array(out, pool.due_date_);
    put_array(out, desc);
    append_bytes(out, arena.data(), arena.size());

    return out;
}

std::expected<NodePool, Error> deserialize(const std::span<const std::byte> bytes)
{
    Reader r{bytes};

    char magic[sizeof(MAGIC)];
    if (const auto e = r.read_raw(magic, sizeof(magic)); !e)
    {
        return std::unexpected(e.error());
    }
    if (std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0)
    {
        return std::unexpected(Error::BAD_MAGIC);
    }

    std::uint32_t version = 0;
    if (const auto e = r.read_u32(version); !e)
    {
        return std::unexpected(e.error());
    }
    if (version != VERSION)
    {
        return std::unexpected(Error::UNSUPPORTED_VERSION);
    }

    std::uint32_t slot_count = 0;
    std::uint32_t free_head = 0;
    std::uint32_t root_index = 0;
    std::uint32_t root_generation = 0;
    std::uint64_t live_count = 0;
    std::uint64_t arena_size = 0;
    if (const auto e = r.read_u32(slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_u32(free_head); !e) return std::unexpected(e.error());
    if (const auto e = r.read_u32(root_index); !e) return std::unexpected(e.error());
    if (const auto e = r.read_u32(root_generation); !e) return std::unexpected(e.error());
    if (const auto e = r.read_u64(live_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_u64(arena_size); !e) return std::unexpected(e.error());

    std::vector<std::uint32_t> generation;
    std::vector<std::uint32_t> next_free;
    std::vector<TaskId> parent;
    std::vector<TaskId> first_child;
    std::vector<TaskId> next_sibling;
    std::vector<Status> status;
    std::vector<Priority> priority;
    std::vector<std::int64_t> due_date;
    std::vector<DescSlice> desc;
    std::vector<char> arena;
    if (const auto e = r.read_array(generation, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(next_free, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(parent, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(first_child, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(next_sibling, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(status, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(priority, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(due_date, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(desc, slot_count); !e) return std::unexpected(e.error());
    if (const auto e = r.read_array(arena, static_cast<std::size_t>(arena_size)); !e)
    {
        return std::unexpected(e.error());
    }

    // Exact-fit: a right-magic, right-version blob with trailing bytes is not a
    // file this build wrote, so refuse rather than ignore the tail.
    if (!r.at_end())
    {
        return std::unexpected(Error::CORRUPT_DATA);
    }

    // Validate the free list and recover which slots are free. A bound on the
    // walk catches a cycle or an index that runs off the end.
    std::vector<bool> is_free(slot_count, false);
    std::uint32_t free_count = 0;
    for (std::uint32_t idx = free_head; idx != NodePool::NIL; idx = next_free[idx])
    {
        if (idx >= slot_count || is_free[idx] || free_count > slot_count)
        {
            return std::unexpected(Error::CORRUPT_DATA);
        }
        is_free[idx] = true;
        ++free_count;
    }
    if (static_cast<std::uint64_t>(slot_count - free_count) != live_count)
    {
        return std::unexpected(Error::CORRUPT_DATA);
    }

    // A live link must name a live slot at the recorded generation; a zero-
    // generation handle is the legitimate "no relation" sentinel.
    const auto link_ok = [&](const TaskId h) noexcept {
        if (!h.is_valid())
        {
            return true;
        }
        return h.index < slot_count && generation[h.index] == h.generation &&
               !is_free[h.index];
    };

    const TaskId root{.index = root_index, .generation = root_generation};
    if (root.is_valid())
    {
        if (!link_ok(root))
        {
            return std::unexpected(Error::CORRUPT_DATA);
        }
    }
    else if (live_count != 0)
    {
        // A non-empty forest must have a root; an invalid root with live nodes
        // is inconsistent.
        return std::unexpected(Error::CORRUPT_DATA);
    }

    for (std::uint32_t i = 0; i < slot_count; ++i)
    {
        if (is_free[i])
        {
            continue;
        }
        if (generation[i] == 0)
        {
            return std::unexpected(Error::CORRUPT_DATA);  // live slots are never gen 0
        }
        if (!link_ok(parent[i]) || !link_ok(first_child[i]) || !link_ok(next_sibling[i]))
        {
            return std::unexpected(Error::CORRUPT_DATA);
        }
        if (const DescSlice &sl = desc[i];
            sl.length != 0 &&
            (sl.offset > arena_size || sl.length > arena_size - sl.offset))
        {
            return std::unexpected(Error::CORRUPT_DATA);
        }
        if (static_cast<std::uint8_t>(status[i]) > static_cast<std::uint8_t>(Status::DONE) ||
            static_cast<std::uint8_t>(priority[i]) > static_cast<std::uint8_t>(Priority::HIGH))
        {
            return std::unexpected(Error::CORRUPT_DATA);
        }
    }

    NodePool pool;
    pool.generation_ = std::move(generation);
    pool.next_free_ = std::move(next_free);
    pool.parent_ = std::move(parent);
    pool.first_child_ = std::move(first_child);
    pool.next_sibling_ = std::move(next_sibling);
    pool.status_ = std::move(status);
    pool.priority_ = std::move(priority);
    pool.due_date_ = std::move(due_date);
    pool.desc_ = std::move(desc);
    pool.desc_arena_ = std::move(arena);
    pool.root_ = root.is_valid() ? root : TaskId::invalid();
    pool.free_head_ = free_head;
    pool.live_count_ = static_cast<std::size_t>(live_count);
    return pool;
}

// --- Filesystem -------------------------------------------------------------

std::filesystem::path tasks_path()
{
    namespace fs = std::filesystem;
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0')
    {
        return fs::path{xdg} / "toe" / "tasks.bin";
    }
    const char *home = std::getenv("HOME");
    const fs::path base = (home != nullptr && *home != '\0') ? fs::path{home} : fs::path{"."};
    return base / ".config" / "toe" / "tasks.bin";
}

std::expected<void, Error> save(const NodePool &pool, const std::filesystem::path &path)
{
    namespace fs = std::filesystem;
    if (path.has_parent_path())
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec)
        {
            return std::unexpected(Error::IO_ERROR);
        }
    }

    std::ofstream os{path, std::ios::binary | std::ios::trunc};
    if (!os)
    {
        return std::unexpected(Error::IO_ERROR);
    }
    const std::vector<std::byte> bytes = serialize(pool);
    os.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!os)
    {
        return std::unexpected(Error::IO_ERROR);
    }
    return {};
}

std::expected<NodePool, Error> load(const std::filesystem::path &path)
{
    std::ifstream is{path, std::ios::binary | std::ios::ate};
    if (!is)
    {
        return std::unexpected(Error::IO_ERROR);
    }
    const std::streamoff size = is.tellg();
    if (size < 0)
    {
        return std::unexpected(Error::IO_ERROR);
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    is.seekg(0);
    if (size != 0)
    {
        is.read(reinterpret_cast<char *>(bytes.data()), size);
        if (is.gcount() != size)
        {
            return std::unexpected(Error::IO_ERROR);
        }
    }
    return deserialize(bytes);
}

}  // namespace toe::core
