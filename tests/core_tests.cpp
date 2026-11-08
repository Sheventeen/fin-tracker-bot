// Tests for everything that doesn't need Discord. Run with:
//   ctest --test-dir build --output-on-failure
//
// Uses its own CHECK macro instead of assert(): assert compiles away when
// NDEBUG is defined (Release builds), which would make every test "pass".

#include <unistd.h>  // getpid

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <variant>

#include "db.hpp"
#include "money.hpp"
#include "parser.hpp"
#include "periods.hpp"
#include "text.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(expr)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(expr)) {                                                                \
            ++g_failures;                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
        }                                                                             \
    } while (0)

namespace chr = std::chrono;
using namespace std::chrono_literals;
using fin::AmountError;

std::int64_t unix_at(chr::year_month_day day, chr::seconds time_of_day = 0s) {
    return (chr::sys_days{day} + time_of_day).time_since_epoch().count();
}

chr::sys_seconds at(chr::year_month_day day, chr::seconds time_of_day = 0s) {
    return chr::sys_seconds{chr::seconds{unix_at(day, time_of_day)}};
}

// ------------------------------------------------------------- amounts

void test_amounts() {
    auto ok = [](const char* s, std::int64_t cents) {
        const fin::AmountResult r = fin::parse_amount_cents(s);
        return r.error == AmountError::None && r.cents == cents;
    };
    auto err = [](const char* s, AmountError e) { return fin::parse_amount_cents(s).error == e; };

    CHECK(ok("3.14", 314));
    CHECK(ok("3", 300));
    CHECK(ok("3.1", 310));
    CHECK(ok(".5", 50));
    CHECK(ok(".05", 5));
    CHECK(ok("0.01", 1));
    CHECK(ok("0010", 1000));
    CHECK(ok("10000000", 1'000'000'000));
    CHECK(ok("10000000.00", 1'000'000'000));

    CHECK(err("3.145", AmountError::TooManyDecimals));
    CHECK(err("3.100", AmountError::TooManyDecimals));
    CHECK(err("3.", AmountError::Invalid));
    CHECK(err(".", AmountError::Invalid));
    CHECK(err("", AmountError::Invalid));
    CHECK(err("1,000", AmountError::Invalid));
    CHECK(err("3.1.4", AmountError::Invalid));
    CHECK(err("3.14f", AmountError::Invalid));
    CHECK(err("$3", AmountError::Invalid));
    CHECK(err("0", AmountError::Zero));
    CHECK(err("0.00", AmountError::Zero));
    CHECK(err("10000000.01", AmountError::TooLarge));
    CHECK(err("99999999999999999999", AmountError::TooLarge));  // would overflow int64
}

// ------------------------------------------------------------- grammar

template <typename T>
const T* as(const fin::Command& c) {
    return std::get_if<T>(&c);
}

void test_parser() {
    {
        const auto c = fin::parse_message("&3.14 f omars restaurant");
        const auto* log = as<fin::LogCmd>(c);
        CHECK(log != nullptr && log->amount_cents == -314 && log->category->code == "f" &&
              log->note == "omars restaurant");
    }
    {
        const auto c = fin::parse_message("&50 I");
        const auto* log = as<fin::LogCmd>(c);
        CHECK(log != nullptr && log->amount_cents == 5000 && log->category->code == "i" && log->note.empty());
    }
    {
        const auto c = fin::parse_message("  &3.14 INV   index fund  ");
        const auto* log = as<fin::LogCmd>(c);
        CHECK(log != nullptr && log->amount_cents == 314 && log->category->code == "inv" &&
              log->note == "index fund");
    }
    {
        const auto c = fin::parse_message("&12 b phone\nbill");
        const auto* log = as<fin::LogCmd>(c);
        CHECK(log != nullptr && log->note == "phone\nbill");
    }
    {
        const auto c = fin::parse_message("&.5 t");
        const auto* log = as<fin::LogCmd>(c);
        CHECK(log != nullptr && log->amount_cents == -50);
    }
    {
        const std::string ok_note(fin::kMaxNoteChars, 'x');
        CHECK(as<fin::LogCmd>(fin::parse_message("&1 f " + ok_note)) != nullptr);
        CHECK(as<fin::ParseError>(fin::parse_message("&1 f " + ok_note + "x")) != nullptr);
    }
    {
        // The limit counts characters, not bytes: 500 emoji (2000 bytes) fit, 501 don't.
        std::string emoji_note;
        for (std::size_t i = 0; i < fin::kMaxNoteChars; ++i) {
            emoji_note += "\xF0\x9F\x8D\x95";  // U+1F355
        }
        CHECK(as<fin::LogCmd>(fin::parse_message("&1 f " + emoji_note)) != nullptr);
        CHECK(as<fin::ParseError>(fin::parse_message("&1 f " + emoji_note + "x")) != nullptr);
    }

    // Expense attempts that must produce an error, not silence.
    for (const char* bad : {"&3.14", "&3.14 x", "&3.14 food", "&$3.14 f", "&-5 f", "&+5 i", "&3.145 f",
                            "&0 f", "&.505 f", "&3. f", "&1,000 f", "&3.1.4 f"}) {
        CHECK(as<fin::ParseError>(fin::parse_message(bad)) != nullptr);
    }

    // Not for the bot.
    for (const char* other : {"hello", "", "&", "& 3.14 f", "&hello", "you & me", "&nbsp", "&1st place!", "&...",
                              "&-_-", "&$$$", "&2nd", "&.", "&-"}) {
        CHECK(as<fin::Ignore>(fin::parse_message(other)) != nullptr);
    }

    {
        const auto c = fin::parse_message("&week -m");
        const auto* t = as<fin::TotalsCmd>(c);
        CHECK(t != nullptr && t->period == fin::Period::Week && t->more && !t->year);
    }
    {
        const auto c = fin::parse_message("&YEAR -M 2025");
        const auto* t = as<fin::TotalsCmd>(c);
        CHECK(t != nullptr && t->period == fin::Period::Year && t->more && t->year == 2025);
    }
    {
        const auto c = fin::parse_message("&today");
        const auto* t = as<fin::TotalsCmd>(c);
        CHECK(t != nullptr && t->period == fin::Period::Today && !t->more);
    }
    CHECK(as<fin::TotalsCmd>(fin::parse_message("&month")) != nullptr);
    CHECK(as<fin::ParseError>(fin::parse_message("&year 1969")) != nullptr);
    CHECK(as<fin::ParseError>(fin::parse_message("&year 2025 2024")) != nullptr);
    CHECK(as<fin::ParseError>(fin::parse_message("&today 2025")) != nullptr);
    CHECK(as<fin::ParseError>(fin::parse_message("&week -x")) != nullptr);

    {
        const auto c = fin::parse_message("&tz America/Toronto");
        const auto* tz = as<fin::TzCmd>(c);
        CHECK(tz != nullptr && tz->zone == "America/Toronto");
    }
    {
        const auto c = fin::parse_message("&TZ");
        const auto* tz = as<fin::TzCmd>(c);
        CHECK(tz != nullptr && !tz->zone);
    }
    CHECK(as<fin::ParseError>(fin::parse_message("&tz America Toronto")) != nullptr);
    CHECK(as<fin::UndoCmd>(fin::parse_message("&undo")) != nullptr);
    CHECK(as<fin::ParseError>(fin::parse_message("&undo 3")) != nullptr);
    CHECK(as<fin::HelpCmd>(fin::parse_message("&Help")) != nullptr);
}

// ------------------------------------------------------------- money & text

void test_money_and_text() {
    CHECK(fin::format_cents(123456) == "$1,234.56");
    CHECK(fin::format_cents(-314) == "$3.14");
    CHECK(fin::format_cents(5) == "$0.05");
    CHECK(fin::format_cents(100000) == "$1,000.00");
    CHECK(fin::format_cents(99999) == "$999.99");
    CHECK(fin::format_cents(1'000'000'000) == "$10,000,000.00");
    CHECK(fin::format_signed_cents(-314) == "-$3.14");
    CHECK(fin::format_signed_cents(5000) == "+$50.00");
    CHECK(fin::format_signed_cents(0) == "$0.00");

    CHECK(fin::neutralize_mentions("@everyone") == "@\xE2\x80\x8B" "everyone");
    CHECK(fin::neutralize_mentions("<@123> hi") == "<@\xE2\x80\x8B" "123> hi");
    CHECK(fin::neutralize_mentions("no mentions") == "no mentions");
    CHECK(fin::trim("  a b \n") == "a b");
    CHECK(fin::iequals_ascii("INV", "inv"));
    CHECK(fin::utf8_length("") == 0);
    CHECK(fin::utf8_length("abc") == 3);
    CHECK(fin::utf8_length("caf\xC3\xA9") == 4);         // é is 2 bytes
    CHECK(fin::utf8_length("\xF0\x9F\x8D\x95!") == 2);  // emoji is 4 bytes
}

// ------------------------------------------------------------- periods

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

// ------------------------------------------------------------- database

fin::Db::NewTransaction tx(std::uint64_t user, std::int64_t cents, const char* cat, std::int64_t when,
                           std::optional<std::uint64_t> message_id, const char* note = "") {
    return {user, cents, cat, note, when, std::nullopt, message_id};
}

void test_db() {
    fin::Db db(":memory:");
    const std::int64_t day = unix_at(2026y / chr::November / 7);
    const std::int64_t next_day = day + 86400;

    // Real Discord-sized snowflakes round-trip through SQLite's signed INTEGER.
    const std::uint64_t alice = 1'234'567'890'123'456'789ULL;
    const std::uint64_t bob = 42;

    CHECK(db.add_transaction(tx(alice, -314, "f", day + 100, 900, "omars")) == fin::Db::InsertResult::Inserted);
    CHECK(db.add_transaction(tx(alice, -314, "f", day + 100, 900, "omars")) == fin::Db::InsertResult::Duplicate);
    CHECK(db.add_transaction(tx(alice, 5000, "i", day + 200, 901)) == fin::Db::InsertResult::Inserted);
    CHECK(db.add_transaction(tx(alice, -2500, "b", day, 902)) == fin::Db::InsertResult::Inserted);       // == start
    CHECK(db.add_transaction(tx(alice, -9999, "f", next_day, 903)) == fin::Db::InsertResult::Inserted);  // == end
    CHECK(db.add_transaction(tx(bob, -700, "f", day + 300, 904)) == fin::Db::InsertResult::Inserted);
    CHECK(db.add_transaction(tx(bob, -100, "t", day + 301, std::nullopt)) == fin::Db::InsertResult::Inserted);
    CHECK(db.add_transaction(tx(bob, -100, "t", day + 301, std::nullopt)) == fin::Db::InsertResult::Inserted);

    // Bad category must raise, not be silently skipped.
    bool threw = false;
    try {
        db.add_transaction(tx(alice, -1, "zzz", day, 999));
    } catch (const fin::DbError&) {
        threw = true;
    }
    CHECK(threw);

    {
        const auto rows = db.totals_by_category(alice, day, next_day);  // [start, end)
        std::int64_t food = 0, income = 0, bill = 0, total_count = 0;
        for (const auto& r : rows) {
            total_count += r.count;
            if (r.category == "f") food = r.sum_cents;
            if (r.category == "i") income = r.sum_cents;
            if (r.category == "b") bill = r.sum_cents;
        }
        CHECK(food == -314);  // the next_day entry is excluded; the duplicate wasn't stored
        CHECK(income == 5000);
        CHECK(bill == -2500);  // start boundary included
        CHECK(total_count == 3);
    }
    {
        const auto rows = db.totals_by_category(bob, day, next_day);
        std::int64_t sum = 0;
        for (const auto& r : rows) sum += r.sum_cents;
        CHECK(sum == -900);  // only bob's rows
    }

    {
        const std::uint64_t any = std::numeric_limits<std::int64_t>::max();  // no message_id cutoff
        // An &undo sent as message 903 (before the next_day entry's message,
        // even if handled after it) skips that entry and takes the next newest.
        const auto skipped = db.undo_last(alice, 903);
        CHECK(skipped && skipped->amount_cents == 5000 && skipped->category == "i");
        const auto removed = db.undo_last(alice, any);  // latest by created_at: the next_day food entry
        CHECK(removed && removed->amount_cents == -9999 && removed->category == "f" &&
              removed->created_at == next_day);
        const auto again = db.undo_last(alice, any);
        CHECK(again && again->amount_cents == -314 && again->note == "omars");
        CHECK(!db.undo_last(alice, 900));  // only message 902 is left, and it was sent after 900
        const auto last = db.undo_last(alice, any);
        CHECK(last && last->category == "b");
        CHECK(!db.undo_last(777, any));  // unknown user
    }
    {
        // Two entries in the same second, inserted out of order: undo follows
        // message order (send order), not insert order.
        CHECK(db.add_transaction(tx(bob, -2, "f", next_day, 951)) == fin::Db::InsertResult::Inserted);
        CHECK(db.add_transaction(tx(bob, -1, "f", next_day, 950)) == fin::Db::InsertResult::Inserted);
        const auto removed = db.undo_last(bob, 952);
        CHECK(removed && removed->amount_cents == -2);
    }

    CHECK(!db.get_timezone(alice));
    CHECK(!db.get_timezone(777));
    db.set_timezone(alice, "America/Toronto", day);
    CHECK(db.get_timezone(alice) == "America/Toronto");
    db.set_timezone(alice, "Europe/London", day);
    CHECK(db.get_timezone(alice) == "Europe/London");
    db.set_timezone(555, "Asia/Tokyo", day);  // user who never logged anything
    CHECK(db.add_transaction(tx(555, -1, "p", day, 905)) == fin::Db::InsertResult::Inserted);
    CHECK(db.get_timezone(555) == "Asia/Tokyo");  // logging doesn't wipe the timezone

    const std::string plan = db.explain_totals_query();
    CHECK(plan.find("COVERING INDEX idx_tx_user_time") != std::string::npos);
    if (plan.find("COVERING INDEX idx_tx_user_time") == std::string::npos) {
        std::fprintf(stderr, "query plan was:\n%s", plan.c_str());
    }
}

void test_db_file_persists() {
    // The PID keeps concurrent runs (ctest -j, several build dirs) from sharing a file.
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("fin-tracker-core-tests-" + std::to_string(::getpid()) + ".db");
    for (const char* suffix : {"", "-wal", "-shm"}) {
        std::filesystem::remove(path.string() + suffix);
    }
    {
        fin::Db db(path.string());
        db.add_transaction(tx(1, -314, "f", 1000, 1));
    }
    {
        fin::Db db(path.string());  // reopen: migration must be a no-op
        const auto rows = db.totals_by_category(1, 0, 2000);
        CHECK(rows.size() == 1 && rows[0].sum_cents == -314);
    }
    for (const char* suffix : {"", "-wal", "-shm"}) {
        std::filesystem::remove(path.string() + suffix);
    }
}

}  // namespace

int main() {
    test_amounts();
    test_parser();
    test_money_and_text();
    test_periods();
    test_db();
    test_db_file_persists();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
