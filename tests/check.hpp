// Shared by every test file: the CHECK macro, its counters, and date helpers.
//
// Uses its own CHECK macro instead of assert(): assert compiles away when
// NDEBUG is defined (Release builds), which would make every test "pass".
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace test {

// inline: one shared counter per test program, even if several files include this.
inline int g_checks = 0;
inline int g_failures = 0;

// Prints the summary and returns the exit code for main(): 0 = all passed.
inline int finish() {
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace test

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        ++test::g_checks;                                                                 \
        if (!(expr)) {                                                                    \
            ++test::g_failures;                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
        }                                                                                 \
    } while (0)

namespace chr = std::chrono;
using namespace std::chrono_literals;

// Unix seconds for a UTC date plus an optional time of day.
inline std::int64_t unix_at(chr::year_month_day day, chr::seconds time_of_day = chr::seconds{0}) {
    return (chr::sys_days{day} + time_of_day).time_since_epoch().count();
}

// Same moment as unix_at, as a chrono time point.
inline chr::sys_seconds at(chr::year_month_day day, chr::seconds time_of_day = chr::seconds{0}) {
    return chr::sys_seconds{chr::seconds{unix_at(day, time_of_day)}};
}
