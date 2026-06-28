#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace toe::ui
{

// Format a Unix epoch (seconds) as "YYYY-MM-DD" in UTC.
[[nodiscard]] inline std::string format_due_date(std::int64_t epoch_seconds)
{
    using namespace std::chrono;
    const auto ymd = year_month_day{floor<days>(sys_seconds{seconds{epoch_seconds}})};
    const int  y = static_cast<int>(ymd.year());
    const auto m = static_cast<unsigned>(ymd.month());
    const auto d = static_cast<unsigned>(ymd.day());
    char buf[11];
    (void)std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d);
    return std::string{buf, 10};
}

// Parse "YYYY-MM-DD" to a Unix epoch (seconds, midnight UTC).
// Returns std::nullopt on any parse or calendar-validity failure.
[[nodiscard]] inline std::optional<std::int64_t> parse_due_date(std::string_view sv)
{
    if (sv.size() != 10 || sv[4] != '-' || sv[7] != '-')
    {
        return std::nullopt;
    }
    const auto digits = [](std::string_view s) -> std::optional<int> {
        int v = 0;
        for (const char c : s)
        {
            if (c < '0' || c > '9')
            {
                return std::nullopt;
            }
            v = v * 10 + (c - '0');
        }
        return v;
    };
    const auto y = digits(sv.substr(0, 4));
    const auto m = digits(sv.substr(5, 2));
    const auto d = digits(sv.substr(8, 2));
    if (!y || !m || !d)
    {
        return std::nullopt;
    }
    using namespace std::chrono;
    const auto ymd = year_month_day{
        year{*y},
        month{static_cast<unsigned>(*m)},
        day{static_cast<unsigned>(*d)},
    };
    if (!ymd.ok())
    {
        return std::nullopt;
    }
    return sys_days{ymd}.time_since_epoch().count() * std::int64_t{86400};
}

}  // namespace toe::ui
