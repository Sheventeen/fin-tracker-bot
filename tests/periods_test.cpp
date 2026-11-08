// Period ranges (day/week/month/year), DST, timezone lookup and labels.
// Run all test files with: ctest --test-dir build --output-on-failure

#include "check.hpp"
#include "periods.hpp"

namespace {

void test_periods() {
    const chr::time_zone* utc = fin::find_time_zone("UTC");
    const chr::time_zone* toronto = fin::find_time_zone("america/toronto");
    CHECK(utc != nullptr);
    CHECK(toronto != nullptr && toronto->name() == "America/Toronto");
    if (utc == nullptr || toronto == nullptr) {
        return;
    }
    const chr::time_zone* ny = fin::find_time_zone("US/Eastern");  // a link
    CHECK(ny != nullptr && ny->name() == "America/New_York");
    CHECK(fin::find_time_zone("Bad/Zone") == nullptr);
    CHECK(fin::find_time_zone("") == nullptr);

    // Saturday 2026-11-07 12:00 UTC.
    const chr::sys_seconds now = at(2026y / chr::November / 7, 12h);

    {
        const auto r = fin::compute_period(fin::Period::Today, std::nullopt, utc, now);
        CHECK(r.start_utc == unix_at(2026y / chr::November / 7));
        CHECK(r.end_utc == unix_at(2026y / chr::November / 8));
        CHECK(r.label == "Today · Sat, Nov 7 2026");
    }
    {
        // Weeks start on Sunday.
        const auto r = fin::compute_period(fin::Period::Week, std::nullopt, utc, now);
        CHECK(r.start_utc == unix_at(2026y / chr::November / 1));
        CHECK(r.end_utc == unix_at(2026y / chr::November / 8));
        CHECK(r.label == "This week · Sun, Nov 1 – Sat, Nov 7 2026");
    }
    {
        // A week spanning New Year shows both years.
        const auto r = fin::compute_period(fin::Period::Week, std::nullopt, utc, at(2026y / chr::December / 31));
        CHECK(r.label == "This week · Sun, Dec 27 2026 – Sat, Jan 2 2027");
    }
    {
        // A Sunday is the first day of its own week.
        const auto r = fin::compute_period(fin::Period::Week, std::nullopt, utc, at(2026y / chr::November / 8, 1h));
        CHECK(r.start_utc == unix_at(2026y / chr::November / 8));
    }
    {
        // Toronto week containing the DST change (2026-11-01 02:00 EDT -> EST):
        // starts at midnight EDT (UTC-4), ends at midnight EST (UTC-5).
        const auto r = fin::compute_period(fin::Period::Week, std::nullopt, toronto, now);
        CHECK(r.start_utc == unix_at(2026y / chr::November / 1, 4h));
        CHECK(r.end_utc == unix_at(2026y / chr::November / 8, 5h));
    }
    {
        // Spring-forward day in Toronto is 23 hours long.
        const auto r = fin::compute_period(fin::Period::Today, std::nullopt, toronto, at(2026y / chr::March / 8, 18h));
        CHECK(r.start_utc == unix_at(2026y / chr::March / 8, 5h));
        CHECK(r.end_utc - r.start_utc == 23 * 3600);
    }
    {
        // Local date differs from UTC date: 02:00 UTC Nov 7 is still Nov 6 in Toronto.
        const auto r = fin::compute_period(fin::Period::Today, std::nullopt, toronto, at(2026y / chr::November / 7, 2h));
        CHECK(r.label == "Today · Fri, Nov 6 2026");
    }
    {
        const auto r = fin::compute_period(fin::Period::Month, std::nullopt, utc, now);
        CHECK(r.start_utc == unix_at(2026y / chr::November / 1));
        CHECK(r.end_utc == unix_at(2026y / chr::December / 1));
        CHECK(r.label == "November 2026");
    }
    {
        const auto r = fin::compute_period(fin::Period::Month, std::nullopt, utc, at(2026y / chr::December / 15));
        CHECK(r.end_utc == unix_at(2027y / chr::January / 1));
    }
    {
        const auto r = fin::compute_period(fin::Period::Year, std::nullopt, utc, now);
        CHECK(r.start_utc == unix_at(2026y / chr::January / 1));
        CHECK(r.label == "Year 2026");
    }
    {
        const auto r = fin::compute_period(fin::Period::Year, 2025, utc, now);
        CHECK(r.start_utc == unix_at(2025y / chr::January / 1));
        CHECK(r.end_utc == unix_at(2026y / chr::January / 1));
        CHECK(r.label == "Year 2025");
    }

    CHECK(fin::format_local_datetime(utc, at(2026y / chr::November / 7, 21h + 14min)) == "Sat, Nov 7 2026 9:14 PM");
    CHECK(fin::format_local_datetime(utc, at(2026y / chr::November / 7)) == "Sat, Nov 7 2026 12:00 AM");
    CHECK(fin::format_local_datetime(utc, at(2026y / chr::November / 7, 12h + 5min)) == "Sat, Nov 7 2026 12:05 PM");
}

}  // namespace

int main() {
    test_periods();
    return test::finish();
}
