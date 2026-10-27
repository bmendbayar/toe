#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/file_scan.hpp"
#include "core/node_pool.hpp"
#include "core/serialization.hpp"

namespace
{

namespace fs = std::filesystem;
using namespace toe::core;

// Fresh scratch tree under the system temp dir, removed on destruction.
class FileScan : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root = fs::temp_directory_path() /
               (std::string{"toe-scan-"} +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root);
        config = root / "config";
        work = root / "work";
        fs::create_directories(config);
        fs::create_directories(work);
    }

    void TearDown() override
    { fs::remove_all(root); }

    static void write_toe(const fs::path &path)
    {
        fs::create_directories(path.parent_path());
        ASSERT_TRUE(save(NodePool{}, path).has_value());
    }

    static void write_junk(const fs::path &path)
    {
        fs::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << "not a toe file";
    }

    fs::path root, config, work;
};

TEST_F(FileScan, MagicDetection)
{
    write_toe(work / "good.toe");
    write_junk(work / "bad.toe");
    std::ofstream{work / "tiny.toe", std::ios::binary} << "TO";

    EXPECT_TRUE(has_toe_magic(work / "good.toe"));
    EXPECT_FALSE(has_toe_magic(work / "bad.toe"));
    EXPECT_FALSE(has_toe_magic(work / "tiny.toe"));
    EXPECT_FALSE(has_toe_magic(work / "missing.toe"));
}

TEST_F(FileScan, FindsToeFilesRecursivelyAndSkipsBadMagic)
{
    write_toe(work / "a.toe");
    write_toe(work / "sub" / "deep" / "b.toe");
    write_junk(work / "fake.toe");
    write_toe(work / "legacy.bin");  // .bin only counts in the config dir

    const auto found = find_toe_files(config, work);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0], work / "a.toe");
    EXPECT_EQ(found[1], work / "sub" / "deep" / "b.toe");
}

TEST_F(FileScan, ConfigDirAcceptsToeAndBinButNotRecursive)
{
    write_toe(config / "tasks.bin");
    write_toe(config / "extra.toe");
    write_junk(config / "junk.bin");
    write_toe(config / "nested" / "hidden.toe");

    const auto found = find_toe_files(config, work);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], config / "extra.toe");
}

TEST_F(FileScan, ConfigFilesComeFirst)
{
    write_toe(work / "a.toe");
    write_toe(config / "z.toe");

    const auto found = find_toe_files(config, work);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0], config / "z.toe");
    EXPECT_EQ(found[1], work / "a.toe");
}

TEST_F(FileScan, SkipsHiddenAndBuildDirs)
{
    write_toe(work / ".git" / "x.toe");
    write_toe(work / "build" / "x.toe");
    write_toe(work / "node_modules" / "x.toe");
    write_toe(work / "target" / "x.toe");
    write_toe(work / "cmake-build-debug" / "x.toe");
    write_toe(work / "keep" / "x.toe");

    const auto found = find_toe_files(config, work);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], work / "keep" / "x.toe");
}

TEST_F(FileScan, RespectsMaxDepth)
{
    write_toe(work / "top.toe");
    write_toe(work / "d1" / "one.toe");
    write_toe(work / "d1" / "d2" / "two.toe");
    write_toe(work / "d1" / "d2" / "d3" / "three.toe");

    const auto found = find_toe_files(config, work, {.max_depth = 2});
    ASSERT_EQ(found.size(), 3u);
    EXPECT_EQ(found[0], work / "d1" / "d2" / "two.toe");
    EXPECT_EQ(found[1], work / "d1" / "one.toe");
    EXPECT_EQ(found[2], work / "top.toe");
}

TEST_F(FileScan, DedupesWhenSearchRootContainsConfigDir)
{
    write_toe(config / "shared.toe");

    const auto found = find_toe_files(config, root);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], config / "shared.toe");
}

TEST_F(FileScan, MissingDirectoriesYieldNothing)
{
    EXPECT_TRUE(find_toe_files(root / "nope", root / "also-nope").empty());
}

}  // namespace
