#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace toe::ui
{

// All dates are handled at day granularity, anchored to midnight UTC, matching
// how Core stores due dates (Unix epoch seconds). Everything here is pure: no
// FTXUI, no calls to the system clock — "now" is always supplied by the caller
// so rendering stays deterministic under test.

inline constexpr std::int64_t kSecondsPerDay = 86400;

// --- epoch <-> calendar ------------------------------------------------------

[[nodiscard]] constexpr std::chrono::year_month_day
epoch_to_ymd(std::int64_t epoch_seconds) noexcept
{
    using namespace std::chrono;
    return year_month_day{floor<days>(sys_seconds{seconds{epoch_seconds}})};
}

[[nodiscard]] constexpr std::int64_t
ymd_to_epoch(std::chrono::year_month_day ymd) noexcept
{
    return std::chrono::sys_days{ymd}.time_since_epoch().count() * kSecondsPerDay;
}

// Floor an epoch to the start of its UTC day. Floor division (toward negative
// infinity) so pre-1970 instants land on the correct day boundary.
[[nodiscard]] constexpr std::int64_t floor_to_day(std::int64_t epoch_seconds) noexcept
{
    std::int64_t q = epoch_seconds / kSecondsPerDay;
    if (epoch_seconds % kSecondsPerDay != 0 && epoch_seconds < 0)
    {
        --q;
    }
    return q * kSecondsPerDay;
}

// Midnight UTC of the day containing `now_seconds`.
[[nodiscard]] constexpr std::int64_t today_epoch(std::int64_t now_seconds) noexcept
{
    return floor_to_day(now_seconds);
}

[[nodiscard]] constexpr std::int64_t add_days(std::int64_t epoch_seconds, std::int64_t n) noexcept
{
    return epoch_seconds + n * kSecondsPerDay;
}

// Add `n` calendar months, clamping the day-of-month to the target month's last
// valid day (e.g. Jan 31 + 1mo -> Feb 28/29). `n` may be negative.
[[nodiscard]] constexpr std::int64_t add_months(std::int64_t epoch_seconds, int n) noexcept
{
    using namespace std::chrono;
    const year_month_day ymd = epoch_to_ymd(epoch_seconds);
    const year_month ym = year_month{ymd.year(), ymd.month()} + months{n};
    const year_month_day_last last{ym / std::chrono::last};
    const unsigned d =
        std::min(static_cast<unsigned>(ymd.day()), static_cast<unsigned>(last.day()));
    return ymd_to_epoch(year_month_day{ym.year(), ym.month(), day{d}});
}

// Layout facts needed to render a month as a weekday grid.
struct MonthGrid
{
    std::chrono::year_month ym;
    unsigned days_in_month;  // 28..31
    unsigned first_weekday;  // 0 = Sunday .. 6 = Saturday
};

[[nodiscard]] constexpr MonthGrid month_grid(std::chrono::year_month ym) noexcept
{
    using namespace std::chrono;
    const year_month_day_last last{ym / std::chrono::last};
    const weekday first{sys_days{ym / day{1}}};
    return MonthGrid{
        .ym = ym,
        .days_in_month = static_cast<unsigned>(last.day()),
        .first_weekday = first.c_encoding(),
    };
}

// --- formatting --------------------------------------------------------------

// Format a Unix epoch (seconds) as "YYYY-MM-DD" in UTC.
[[nodiscard]] inline std::string format_due_date(std::int64_t epoch_seconds)
{
    const auto ymd = epoch_to_ymd(epoch_seconds);
    const int y = static_cast<int>(ymd.year());
    const auto m = static_cast<unsigned>(ymd.month());
    const auto d = static_cast<unsigned>(ymd.day());
    char buf[11];
    (void)std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d);
    return std::string{buf, 10};
}

// Human-relative description of a due date against "now": "today", "tomorrow",
// "yesterday", "in 3d", "2w overdue", etc. Days under two weeks, weeks beyond.
[[nodiscard]] inline std::string format_relative(std::int64_t due_epoch, std::int64_t now_seconds)
{
    const std::int64_t diff_days =
        (floor_to_day(due_epoch) - today_epoch(now_seconds)) / kSecondsPerDay;
    if (diff_days == 0)
    {
        return "today";
    }
    if (diff_days == 1)
    {
        return "tomorrow";
    }
    if (diff_days == -1)
    {
        return "yesterday";
    }
    const std::int64_t mag = diff_days < 0 ? -diff_days : diff_days;
    char buf[32];
    if (mag < 14)
    {
        (void)std::snprintf(buf, sizeof(buf), diff_days > 0 ? "in %lldd" : "%lldd overdue", static_cast<long long>(mag));
    }
    else
    {
        (void)std::snprintf(buf, sizeof(buf), diff_days > 0 ? "in %lldw" : "%lldw overdue", static_cast<long long>(mag / 7));
    }
    return std::string{buf};
}

// --- urgency -----------------------------------------------------------------

enum class Urgency : std::uint8_t
{
    Overdue,  // due day is strictly before today
    Soon,     // due within `soon_days` (today counts)
    Normal,
};

[[nodiscard]] constexpr Urgency
due_urgency(std::int64_t due_epoch, std::int64_t now_seconds, std::int64_t soon_days = 3) noexcept
{
    const std::int64_t due_day = floor_to_day(due_epoch);
    const std::int64_t today = today_epoch(now_seconds);
    if (due_day < today)
    {
        return Urgency::Overdue;
    }
    if ((due_day - today) / kSecondsPerDay <= soon_days)
    {
        return Urgency::Soon;
    }
    return Urgency::Normal;
}

// --- parsing -----------------------------------------------------------------

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
    return ymd_to_epoch(ymd);
}

// Three-letter lowercase weekday abbreviation -> chrono weekday (Sunday = 0).
[[nodiscard]] inline std::optional<std::chrono::weekday> parse_weekday(std::string_view sv)
{
    using namespace std::chrono;
    if (sv == "sun") return Sunday;
    if (sv == "mon") return Monday;
    if (sv == "tue") return Tuesday;
    if (sv == "wed") return Wednesday;
    if (sv == "thu") return Thursday;
    if (sv == "fri") return Friday;
    if (sv == "sat") return Saturday;
    return std::nullopt;
}

// Parse a relative date expression against `now`. Accepts (lowercase):
//   today | tomorrow | yesterday | eom (end of current month)
//   a weekday abbreviation (mon..sun) — the soonest such day, today inclusive
//   a signed offset [+-]N(d|w|m) — days, weeks, or months from today
// Returns std::nullopt if the expression matches none of these.
[[nodiscard]] inline std::optional<std::int64_t>
parse_relative_date(std::string_view sv, std::int64_t now_seconds)
{
    using namespace std::chrono;
    const std::int64_t today = today_epoch(now_seconds);

    if (sv.empty())
    {
        return std::nullopt;
    }
    if (sv == "today")
    {
        return today;
    }
    if (sv == "tomorrow")
    {
        return add_days(today, 1);
    }
    if (sv == "yesterday")
    {
        return add_days(today, -1);
    }
    if (sv == "eom")
    {
        const auto ymd = epoch_to_ymd(today);
        const year_month_day_last last{year_month{ymd.year(), ymd.month()} / std::chrono::last};
        return ymd_to_epoch(year_month_day{last});
    }
    if (const auto wd = parse_weekday(sv); wd.has_value())
    {
        const weekday cur{sys_days{epoch_to_ymd(today)}};
        const auto diff = (*wd - cur).count();  // [0, 6], today counts as 0
        return add_days(today, diff);
    }

    // Signed offset: a leading '+' or '-' is required to disambiguate from the
    // named forms above.
    const char sign_c = sv.front();
    if (sign_c != '+' && sign_c != '-')
    {
        return std::nullopt;
    }
    const std::int64_t sign = (sign_c == '-') ? -1 : 1;
    std::size_t i = 1;
    std::int64_t mag = 0;
    bool any_digit = false;
    while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9')
    {
        mag = mag * 10 + (sv[i] - '0');
        any_digit = true;
        ++i;
    }
    // Exactly one unit character must remain after the digits.
    if (!any_digit || i != sv.size() - 1)
    {
        return std::nullopt;
    }
    const std::int64_t v = sign * mag;
    switch (sv[i])
    {
        case 'd':
            return add_days(today, v);
        case 'w':
            return add_days(today, v * 7);
        case 'm':
            return add_months(today, static_cast<int>(v));
        default:
            return std::nullopt;
    }
}

// Unified entry point used by the UI: try absolute "YYYY-MM-DD" first, then the
// relative grammar. Returns std::nullopt only if neither matches.
[[nodiscard]] inline std::optional<std::int64_t>
parse_date_input(std::string_view sv, std::int64_t now_seconds)
{
    if (const auto abs = parse_due_date(sv); abs.has_value())
    {
        return abs;
    }
    return parse_relative_date(sv, now_seconds);
}

}  // namespace toe::ui
