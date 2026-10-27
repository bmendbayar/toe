#include "core/file_scan.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include "core/serialization.hpp"

namespace toe::core
{

namespace
{

namespace fs = std::filesystem;

bool is_skipped_dir(const std::string &name)
{
    if (name.starts_with('.'))
    {
        return true;
    }
    return name == "build" || name == "node_modules" || name == "target" ||
           name.starts_with("cmake-build-");
}

void sort_paths(std::vector<fs::path> &paths)
{
    std::ranges::sort(paths);
}

}  // namespace

bool has_toe_magic(const fs::path &path)
{
    std::ifstream is{path, std::ios::binary};
    std::array<char, sizeof(MAGIC)> buf{};
    if (!is.read(buf.data(), static_cast<std::streamsize>(buf.size())))
    {
        return false;
    }
    return std::memcmp(buf.data(), MAGIC, sizeof(MAGIC)) == 0;
}

std::vector<fs::path>
find_toe_files(const fs::path &config_dir, const fs::path &search_root, const ScanOptions options)
{
    std::vector<fs::path> config_files;
    std::vector<fs::path> root_files;

    std::error_code ec;
    for (fs::directory_iterator it{config_dir, fs::directory_options::skip_permission_denied, ec},
         end;
         !ec && it != end;
         it.increment(ec))
    {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec))
        {
            continue;
        }
        const auto ext = it->path().extension();
        if ((ext == ".toe") && has_toe_magic(it->path()))
        {
            config_files.push_back(it->path());
        }
    }

    ec.clear();
    for (fs::recursive_directory_iterator
             it{search_root, fs::directory_options::skip_permission_denied, ec},
         end;
         !ec && it != end;
         it.increment(ec))
    {
        std::error_code entry_ec;
        if (it->is_symlink(entry_ec))
        {
            continue;
        }
        if (it->is_directory(entry_ec))
        {
            if (is_skipped_dir(it->path().filename().string()) || it.depth() >= options.max_depth)
            {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (it->is_regular_file(entry_ec) && it->path().extension() == ".toe" &&
            has_toe_magic(it->path()))
        {
            root_files.push_back(it->path());
        }
    }

    sort_paths(config_files);
    sort_paths(root_files);

    std::vector<fs::path> result;
    std::set<fs::path> seen;
    const auto add = [&](const fs::path &p) {
        std::error_code canon_ec;
        const auto canonical = fs::weakly_canonical(p, canon_ec);
        if (seen.insert(canon_ec ? p : canonical).second)
        {
            result.push_back(p);
        }
    };
    for (const auto &p : config_files)
    {
        add(p);
    }
    for (const auto &p : root_files)
    {
        add(p);
    }
    return result;
}

}  // namespace toe::core
