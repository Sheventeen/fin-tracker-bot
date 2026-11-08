// Database: inserts, duplicates, undo, timezones, totals, the covering index, and reopening a file.
// Run all test files with: ctest --test-dir build --output-on-failure

#include <unistd.h>  // getpid

#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

#include "check.hpp"
#include "db.hpp"

namespace {

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
        using Status = fin::Db::UndoStatus;
        // Undo message IDs far above every entry's, so nothing is cut off.
        // Each &undo is a different message, so each needs its own ID.
        std::uint64_t later = std::numeric_limits<std::int64_t>::max();
        // An &undo sent as message 903 (before the next_day entry's message,
        // even if handled after it) skips that entry and takes the next newest.
        const auto skipped = db.undo_last(alice, 903);
        CHECK(skipped.status == Status::Removed && skipped.removed.amount_cents == 5000 &&
              skipped.removed.category == "i");
        // The same &undo delivered again (a replayed event) must not delete another entry.
        CHECK(db.undo_last(alice, 903).status == Status::AlreadyDone);
        const auto removed = db.undo_last(alice, later--);  // latest by created_at: the next_day food entry
        CHECK(removed.status == Status::Removed && removed.removed.amount_cents == -9999 &&
              removed.removed.category == "f" && removed.removed.created_at == next_day);
        const auto again = db.undo_last(alice, later--);
        CHECK(again.status == Status::Removed && again.removed.amount_cents == -314 &&
              again.removed.note == "omars");
        // Only message 902 is left, and it was sent after 900.
        CHECK(db.undo_last(alice, 900).status == Status::NothingToUndo);
        const auto last = db.undo_last(alice, later--);
        CHECK(last.status == Status::Removed && last.removed.category == "b");
        CHECK(db.undo_last(777, later--).status == Status::NothingToUndo);  // unknown user
    }
    {
        // Two entries in the same second, inserted out of order: undo follows
        // message order (send order), not insert order.
        CHECK(db.add_transaction(tx(bob, -2, "f", next_day, 951)) == fin::Db::InsertResult::Inserted);
        CHECK(db.add_transaction(tx(bob, -1, "f", next_day, 950)) == fin::Db::InsertResult::Inserted);
        const auto removed = db.undo_last(bob, 952);
        CHECK(removed.status == fin::Db::UndoStatus::Removed && removed.removed.amount_cents == -2);
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
    test_db();
    test_db_file_persists();
    return test::finish();
}
