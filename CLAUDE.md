# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

fin-tracker-bot is a Discord bot for tracking personal spending, written in C++. Users log entries with
`&`-prefixed messages (`&3.14 f omars restaurant`). Data belongs to the Discord **user**, not the server: it is
keyed by user ID, so it follows them across every server and DM.

## Stack

- C++20, built with CMake. GCC 14+ is required for `std::chrono` timezone support (tzdb from `/usr/share/zoneinfo`).
- Discord library: [D++ (DPP)](https://dpp.dev) 10.x, installed system-wide from the official `.deb` and found with `find_package(dpp)`. It is not vendored or fetched by CMake. Its CMake config also needs `libopus-dev` installed, even though the bot doesn't use voice.
- SQLite via the raw C API (`libsqlite3-dev`, found with `find_package(SQLite3)`), wrapped in small RAII classes in `src/db.*`.
- The bot reads message text, so the privileged **Message Content Intent** must be enabled in the Developer Portal; without it Discord closes the connection with code 4014.
- No linter yet.

## Commands

```bash
cmake -B build && cmake --build build        # build
ctest --test-dir build --output-on-failure   # run tests (tests/*_test.cpp, one executable each)
set -a; source .env; set +a                  # load DISCORD_TOKEN, DEFAULT_TZ, DB_PATH
./build/fin-tracker-bot                      # run
```

## Structure

- `src/main.cpp`: reads config from the environment, opens the database, creates the `dpp::cluster` with the message-content intent, and routes `on_message_create` to `CommandHandler`. It also deletes leftover global slash commands on startup.
- `src/commands.*` (uses DPP): `CommandHandler` runs parsed commands and owns the reply rules. Expenses get a ✅ reaction (text fallback if reacting fails). Anything with the user's data (totals, `&undo`, `&tz`) goes by DM when sent from a server. Help and parse errors reply in the channel. All bot messages disable mentions, and echoed user text goes through `safe_user_text`. DPP's thread pool doesn't keep event order, so `&undo` waits ~2 s and only removes entries from messages sent before it.
- The `fin-tracker-core` library has no DPP dependency, so tests link it alone:
  - `src/parser.*`: pure `parse_message` → `std::variant` of command structs. Add a new command here first.
  - `src/periods.*`: day, week (Sunday start), month and year ranges in a user's timezone, as UTC `[start, end)`; case-insensitive timezone lookup.
  - `src/db.*`: the only SQL-aware code. A mutex makes it thread-safe (DPP handlers run on a thread pool). Schema migrations are keyed off `PRAGMA user_version`. The `categories` table is synced from `src/categories.hpp` on startup.
  - `src/categories.hpp`: the type codes (f/p/t/d/b/s/i/inv) and their kind (spending, income, investment), which sets both the sign and the totals line they count toward. Never hardcode codes elsewhere.
  - `src/money.*`, `src/text.*`: formatting and string helpers.

## Data model

Money is stored as signed integer cents; the category decides the sign. Timestamps are unix seconds UTC, taken from the Discord message's own send time. `idx_tx_user_time (user_id, created_at, category, amount_cents)` is a covering index for the totals query, and a test asserts the query plan uses it. `message_id` is UNIQUE, so a replayed gateway event can't log the same entry twice. Use `ON CONFLICT(...) DO NOTHING`, never `INSERT OR IGNORE`, which also hides other constraint failures. The same goes for `&undo`: `processed_undos` (schema v2) records each undo message's ID in the same transaction as the delete, so a replayed `&undo` removes nothing more.

## Configuration

`.env` (git-ignored) holds `DISCORD_TOKEN` (required), `DEFAULT_TZ` (IANA name, default `UTC`) and `DB_PATH` (default `data/fin-tracker.db`, relative to the launch folder; the absolute path is printed at startup). The program doesn't parse `.env` itself, so the shell must export it before running.
