#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "parser.hpp"

namespace fin {

// A half-open UTC range [start_utc, end_utc) in unix seconds, plus a
// human-readable description of the local period.
struct PeriodRange {
    std::int64_t start_utc;
    std::int64_t end_utc;
    std::string label;  // "Today · Sat, Nov 7 2026", "This week · Sun, Nov 1 – Sat, Nov 7 2026", ...
};

// Computes the calendar period containing `now` in `tz`. Weeks start on
// Sunday. `year` only applies to Period::Year (defaults to the current year).
PeriodRange compute_period(Period period, std::optional<int> year, const std::chrono::time_zone* tz,
                           std::chrono::sys_seconds now);

// Case-insensitive lookup of an IANA zone or link name ("america/toronto",
// "US/Eastern"). Returns nullptr if unknown. The returned zone's name() is the
// canonical spelling to store.
const std::chrono::time_zone* find_time_zone(std::string_view name);

// "Sat, Nov 7 2026 9:14 PM" in `tz`.
std::string format_local_datetime(const std::chrono::time_zone* tz, std::chrono::sys_seconds t);

}  // namespace fin
