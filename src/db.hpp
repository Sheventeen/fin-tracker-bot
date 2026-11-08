#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace fin {

class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// RAII wrapper around a prepared statement. Movable, not copyable.
class Statement {
public:
    Statement() = default;
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // Parameter indexes are 1-based, as in SQLite.
    void bind(int index, std::int64_t value);
    void bind(int index, std::string_view value);
    void bind(int index, std::optional<std::int64_t> value);  // nullopt -> NULL

    // Returns true when a row is available, false when the statement is done.
    bool step();
    // Clears bindings and rewinds so the statement can be reused.
    void reset() noexcept;

    std::int64_t column_int64(int index) const;
    std::string column_text(int index) const;

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

// The only SQL-aware unit in the program. Thread-safe: every public method
// takes an internal lock, so DPP's thread pool can call it freely.
class Db {
public:
    // Opens (creating if needed) the database at `path` and runs migrations.
    // ":memory:" works for tests.
    explicit Db(const std::string& path);
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    struct NewTransaction {
        std::uint64_t user_id;
        std::int64_t amount_cents;  // signed
        std::string category;       // a code from kCategories
        std::string note;
        std::int64_t created_at;    // unix seconds UTC
        std::optional<std::uint64_t> guild_id;    // nullopt = DM
        std::optional<std::uint64_t> message_id;  // dedupe key
    };
    enum class InsertResult { Inserted, Duplicate };
    InsertResult add_transaction(const NewTransaction& tx);

    struct RemovedTransaction {
        std::int64_t amount_cents;
        std::string category;
        std::string note;
        std::int64_t created_at;
    };
    enum class UndoStatus {
        Removed,        // `removed` holds the deleted entry
        NothingToUndo,  // no entry older than the &undo message
        AlreadyDone,    // this &undo message was handled before (a replayed event)
    };
    struct UndoResult {
        UndoStatus status;
        RemovedTransaction removed{};  // only meaningful when status == Removed
    };
    // Handles the &undo sent as message `undo_message_id`: deletes and returns
    // the user's most recent entry logged by an older message (Discord
    // snowflakes increase with send time). Entries without a message_id always
    // qualify. Each undo message is acted on at most once, so a replayed
    // gateway event can't delete a second entry.
    UndoResult undo_last(std::uint64_t user_id, std::uint64_t undo_message_id);

    // nullopt when the user hasn't chosen one (or doesn't exist yet).
    std::optional<std::string> get_timezone(std::uint64_t user_id);
    void set_timezone(std::uint64_t user_id, std::string_view timezone, std::int64_t now);

    struct CategoryTotal {
        std::string category;
        std::int64_t sum_cents;
        std::int64_t count;
    };
    // Sums per category for created_at in [start_utc, end_utc).
    std::vector<CategoryTotal> totals_by_category(std::uint64_t user_id, std::int64_t start_utc,
                                                  std::int64_t end_utc);

    // EXPLAIN QUERY PLAN of the totals query, for tests that guard the index.
    std::string explain_totals_query();

private:
    void exec(const char* sql);
    void migrate();
    void sync_categories();

    struct ConnectionCloser {
        void operator()(sqlite3* db) const noexcept;
    };

    std::mutex mu_;
    // Declared before the statements so it is destroyed after them: SQLite
    // can't close a connection that still has unfinalized statements.
    std::unique_ptr<sqlite3, ConnectionCloser> conn_;
    Statement upsert_user_;
    Statement insert_tx_;
    Statement mark_undo_;
    Statement undo_last_;
    Statement get_tz_;
    Statement set_tz_;
    Statement totals_;
};

}  // namespace fin
