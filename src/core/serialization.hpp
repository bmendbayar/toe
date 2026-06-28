#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "core/node_pool.hpp"

namespace toe::core
{

// Binary persistence for the task forest.
//
// The on-disk format is a *faithful snapshot* of the pool: a fixed header
// (magic, version, slot count, free-list head, root handle, live count, and the
// compacted arena size) followed by every parallel slot array blitted verbatim,
// then the description arena. Slots are preserved one-for-one, so handles —
// including their generations and the free list — survive a save/load round
// trip unchanged. The only thing dropped is orphaned description bytes: the
// arena is compacted (live nodes only) on write, per the project's storage
// decision, and freed slots are persisted blank so the file is deterministic.
//
// Layout assumes little-endian (static_asserted in the .cpp); there is no
// per-field byte swapping. Everything below reports failure through
// std::expected<T, Error> — no exceptions, no partial reads trusted.

// Encode `pool` to a fresh byte buffer. Pure and infallible (allocation aside).
[[nodiscard]] std::vector<std::byte> serialize(const NodePool &pool);

// Decode a pool from `bytes`. Validates magic, version, structural bounds, free
// list integrity, and trailing length before handing back a usable pool:
//   BadMagic            — wrong leading 4 bytes.
//   UnsupportedVersion  — magic ok, version this build cannot read.
//   TruncatedData       — buffer shorter than the header declares.
//   CorruptData         — right length, internally inconsistent.
[[nodiscard]] std::expected<NodePool, Error> deserialize(std::span<const std::byte> bytes);

// Resolve the save-file path per the XDG Base Directory spec:
// `$XDG_CONFIG_HOME/toe/tasks.bin`, falling back to `~/.config/toe/tasks.bin`.
[[nodiscard]] std::filesystem::path tasks_path();

// Serialize `pool` and write it to `path`, creating parent directories as
// needed. Filesystem failures report Error::IoError.
[[nodiscard]] std::expected<void, Error>
save(const NodePool &pool, const std::filesystem::path &path);

// Read `path` and deserialize it. A filesystem failure (missing file, read
// error) reports Error::IoError; a malformed blob reports the deserialize error.
[[nodiscard]] std::expected<NodePool, Error> load(const std::filesystem::path &path);

}  // namespace toe::core
