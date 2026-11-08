#include "db.hpp"

#include <sqlite3.h>

#include <limits>
#include <utility>

#include "categories.hpp"

namespace fin {

namespace {

constexpr int kSchemaVersion = 2;

// Discord snowflakes are uint64 but stay below 2^63 until ~2084, so they fit
// SQLite's signed 64-bit INTEGER exactly.
std::int64_t to_db_id(std::uint64_t id) {
    return static_cast<std::int64_t>(id);
}

std::optional<std::int64_t> to_db_id(std::optional<std::uint64_t> id) {
    if (!id) {
        return std::nullopt;
    }
    return to_db_id(*id);
}

[[noreturn]] void throw_error(sqlite3* db, const std::string& context) {
    throw DbError(context + ": " + (db != nullptr ? sqlite3_errmsg(db) : "out of memory"));
}

// Rewinds a cached statement on scope exit, even if a step threw.
class ResetGuard {
public:
    explicit ResetGuard(Statement& stmt) : stmt_(stmt) {}
    ~ResetGuard() { stmt_.reset(); }
    ResetGuard(const ResetGuard&) = delete;
    ResetGuard& operator=(const ResetGuard&) = delete;

private:
    Statement& stmt_;
};

// v2: remembers which &undo messages were already handled, so a replayed
// event can't undo twice. Keyed by message ID alone: snowflakes are globally unique.
constexpr const char* kSchemaV2 = R"sql(
CREATE TABLE processed_undos (
    message_id INTEGER PRIMARY KEY
);
)sql";

constexpr const char* kSchemaV1 = R"sql(
CREATE TABLE users (
    user_id    INTEGER PRIMARY KEY,
    timezone   TEXT,
    created_at INTEGER NOT NULL
);

CREATE TABLE categories (
    code TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    sign INTEGER NOT NULL CHECK (sign IN (-1, 1))
) WITHOUT ROWID;

CREATE TABLE transactions (
    id           INTEGER PRIMARY KEY,
    user_id      INTEGER NOT NULL REFERENCES users(user_id),
    amount_cents INTEGER NOT NULL,
    category     TEXT    NOT NULL REFERENCES categories(code),
    note         TEXT    NOT NULL DEFAULT '',
    created_at   INTEGER NOT NULL,
    guild_id     INTEGER,
    message_id   INTEGER UNIQUE
);

-- Covering index: per-user time-range totals are answered from the index
-- alone, without reading the table.
CREATE INDEX idx_tx_user_time ON transactions (user_id, created_at, category, amount_cents);
)sql";

constexpr const char* kTotalsSql =
    "SELECT category, SUM(amount_cents), COUNT(*) FROM transactions "
    "WHERE user_id = ?1 AND created_at >= ?2 AND created_at < ?3 "
    "GROUP BY category";

}  // namespace

// ---------------------------------------------------------------- Statement

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK) {
        throw_error(db, "prepare failed for `" + std::string(sql) + "`");
    }
}

Statement::~Statement() {
    sqlite3_finalize(stmt_);  // no-op on nullptr
}

Statement::Statement(Statement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        sqlite3_finalize(stmt_);
        db_ = std::exchange(other.db_, nullptr);
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

void Statement::bind(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
        throw_error(db_, "bind failed");
    }
}

void Statement::bind(int index, std::string_view value) {
    if (sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
        SQLITE_OK) {
        throw_error(db_, "bind failed");
    }
}

void Statement::bind(int index, std::optional<std::int64_t> value) {
    const int rc = value ? sqlite3_bind_int64(stmt_, index, *value) : sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) {
        throw_error(db_, "bind failed");
    }
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw_error(db_, "step failed");
}

void Statement::reset() noexcept {
    if (stmt_ != nullptr) {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }
}

std::int64_t Statement::column_int64(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

std::string Statement::column_text(int index) const {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
    const int size = sqlite3_column_bytes(stmt_, index);
    return text != nullptr ? std::string(text, static_cast<std::size_t>(size)) : std::string{};
}

// ---------------------------------------------------------------- Db

void Db::ConnectionCloser::operator()(sqlite3* db) const noexcept {
    sqlite3_close(db);
}

// If anything below throws, the members already built (the statements, then
// the connection) are destroyed in reverse order, so cleanup needs no code here.
Db::Db(const std::string& path) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    conn_.reset(raw);  // SQLite may return a handle even on failure; it must still be closed
    if (rc != SQLITE_OK) {
        throw DbError("cannot open database '" + path +
                      "': " + (raw != nullptr ? sqlite3_errmsg(raw) : "out of memory"));
    }

    sqlite3* db = conn_.get();
    sqlite3_busy_timeout(db, 5000);
    exec("PRAGMA journal_mode = WAL");    // readers never block the writer
    exec("PRAGMA synchronous = FULL");    // a committed entry survives power loss
    exec("PRAGMA foreign_keys = ON");     // enforce users/categories references
    migrate();
    sync_categories();

    upsert_user_ = Statement(db,
        "INSERT INTO users (user_id, created_at) VALUES (?1, ?2) "
        "ON CONFLICT (user_id) DO NOTHING");
    // Only a duplicate message_id is skipped; any other constraint failure
    // still raises an error (unlike INSERT OR IGNORE).
    insert_tx_ = Statement(db,
        "INSERT INTO transactions (user_id, amount_cents, category, note, created_at, guild_id, message_id) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) "
        "ON CONFLICT (message_id) DO NOTHING");
    // message_id breaks ties within the same second: it follows send order,
    // while `id` follows insert order, which the thread pool can shuffle.
    mark_undo_ = Statement(db,
        "INSERT INTO processed_undos (message_id) VALUES (?1) "
        "ON CONFLICT (message_id) DO NOTHING");
    undo_last_ = Statement(db,
        "DELETE FROM transactions WHERE id = ("
        "  SELECT id FROM transactions WHERE user_id = ?1 AND (message_id IS NULL OR message_id < ?2) "
        "  ORDER BY created_at DESC, message_id DESC, id DESC LIMIT 1"
        ") RETURNING amount_cents, category, note, created_at");
    get_tz_ = Statement(db, "SELECT timezone FROM users WHERE user_id = ?1");
    set_tz_ = Statement(db,
        "INSERT INTO users (user_id, timezone, created_at) VALUES (?1, ?2, ?3) "
        "ON CONFLICT (user_id) DO UPDATE SET timezone = excluded.timezone");
    totals_ = Statement(db, kTotalsSql);
}

Db::~Db() = default;  // statements first, then the connection (see db.hpp)

void Db::exec(const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(conn_.get(), sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error != nullptr ? error : "unknown error";
        sqlite3_free(error);
        throw DbError(std::string("exec failed: ") + message);
    }
}

void Db::migrate() {
    Statement version_stmt(conn_.get(), "PRAGMA user_version");
    version_stmt.step();
    const std::int64_t version = version_stmt.column_int64(0);

    if (version > kSchemaVersion) {
        throw DbError("database schema version " + std::to_string(version) +
                      " is newer than this build supports (" + std::to_string(kSchemaVersion) + ")");
    }
    if (version < 1) {
        exec("BEGIN IMMEDIATE");
        try {
            exec(kSchemaV1);
            exec("PRAGMA user_version = 1");
            exec("COMMIT");
        } catch (...) {
            sqlite3_exec(conn_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
    }
    if (version < 2) {
        exec("BEGIN IMMEDIATE");
        try {
            exec(kSchemaV2);
            exec("PRAGMA user_version = 2");
            exec("COMMIT");
        } catch (...) {
            sqlite3_exec(conn_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
    }
    // Future migrations: `if (version < 3) { ... PRAGMA user_version = 3 ... }`
}

void Db::sync_categories() {
    exec("BEGIN IMMEDIATE");
    try {
        Statement upsert(conn_.get(),
            "INSERT INTO categories (code, name, sign) VALUES (?1, ?2, ?3) "
            "ON CONFLICT (code) DO UPDATE SET name = excluded.name, sign = excluded.sign");
        for (const Category& c : kCategories) {
            upsert.bind(1, c.code);
            upsert.bind(2, c.name);
            upsert.bind(3, std::int64_t{c.sign()});
            upsert.step();
            upsert.reset();
        }
        exec("COMMIT");
    } catch (...) {
        sqlite3_exec(conn_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

Db::InsertResult Db::add_transaction(const NewTransaction& tx) {
    std::lock_guard lock(mu_);
    exec("BEGIN IMMEDIATE");
    try {
        {
            ResetGuard guard(upsert_user_);
            upsert_user_.bind(1, to_db_id(tx.user_id));
            upsert_user_.bind(2, tx.created_at);
            upsert_user_.step();
        }
        bool inserted = false;
        {
            ResetGuard guard(insert_tx_);
            insert_tx_.bind(1, to_db_id(tx.user_id));
            insert_tx_.bind(2, tx.amount_cents);
            insert_tx_.bind(3, tx.category);
            insert_tx_.bind(4, tx.note);
            insert_tx_.bind(5, tx.created_at);
            insert_tx_.bind(6, to_db_id(tx.guild_id));
            insert_tx_.bind(7, to_db_id(tx.message_id));
            insert_tx_.step();
            inserted = sqlite3_changes(conn_.get()) > 0;
        }
        exec("COMMIT");
        return inserted ? InsertResult::Inserted : InsertResult::Duplicate;
    } catch (...) {
        sqlite3_exec(conn_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

Db::UndoResult Db::undo_last(std::uint64_t user_id, std::uint64_t undo_message_id) {
    std::lock_guard lock(mu_);
    // One transaction: recording the undo message and deleting the entry
    // happen together or not at all.
    exec("BEGIN IMMEDIATE");
    try {
        {
            ResetGuard guard(mark_undo_);
            mark_undo_.bind(1, to_db_id(undo_message_id));
            mark_undo_.step();
            if (sqlite3_changes(conn_.get()) == 0) {
                exec("COMMIT");
                return {UndoStatus::AlreadyDone};
            }
        }
        UndoResult result{UndoStatus::NothingToUndo};
        {
            ResetGuard guard(undo_last_);
            undo_last_.bind(1, to_db_id(user_id));
            undo_last_.bind(2, to_db_id(undo_message_id));
            if (undo_last_.step()) {
                result = {UndoStatus::Removed,
                          RemovedTransaction{undo_last_.column_int64(0), undo_last_.column_text(1),
                                             undo_last_.column_text(2), undo_last_.column_int64(3)}};
                // Finish the statement so the DELETE completes before COMMIT.
                while (undo_last_.step()) {
                }
            }
        }
        exec("COMMIT");
        return result;
    } catch (...) {
        sqlite3_exec(conn_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::optional<std::string> Db::get_timezone(std::uint64_t user_id) {
    std::lock_guard lock(mu_);
    ResetGuard guard(get_tz_);
    get_tz_.bind(1, to_db_id(user_id));
    if (!get_tz_.step()) {
        return std::nullopt;
    }
    std::string tz = get_tz_.column_text(0);
    if (tz.empty()) {
        return std::nullopt;  // NULL column
    }
    return tz;
}

void Db::set_timezone(std::uint64_t user_id, std::string_view timezone, std::int64_t now) {
    std::lock_guard lock(mu_);
    ResetGuard guard(set_tz_);
    set_tz_.bind(1, to_db_id(user_id));
    set_tz_.bind(2, timezone);
    set_tz_.bind(3, now);
    set_tz_.step();
}

std::vector<Db::CategoryTotal> Db::totals_by_category(std::uint64_t user_id, std::int64_t start_utc,
                                                      std::int64_t end_utc) {
    std::lock_guard lock(mu_);
    ResetGuard guard(totals_);
    totals_.bind(1, to_db_id(user_id));
    totals_.bind(2, start_utc);
    totals_.bind(3, end_utc);
    std::vector<CategoryTotal> rows;
    while (totals_.step()) {
        rows.push_back({totals_.column_text(0), totals_.column_int64(1), totals_.column_int64(2)});
    }
    return rows;
}

std::string Db::explain_totals_query() {
    std::lock_guard lock(mu_);
    Statement stmt(conn_.get(), std::string("EXPLAIN QUERY PLAN ") + kTotalsSql);
    stmt.bind(1, std::int64_t{0});
    stmt.bind(2, std::int64_t{0});
    stmt.bind(3, std::numeric_limits<std::int64_t>::max());
    std::string plan;
    while (stmt.step()) {
        plan += stmt.column_text(3);  // the "detail" column
        plan += '\n';
    }
    return plan;
}

}  // namespace fin
