#include <dpp/dpp.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "commands.hpp"
#include "db.hpp"
#include "periods.hpp"

namespace {

std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

}  // namespace

int main() {
    const std::string token = env_or("DISCORD_TOKEN", "");
    if (token.empty()) {
        std::cerr << "DISCORD_TOKEN is not set. Put it in .env and load it first.\n";
        return 1;
    }

    // Fail fast on a bad DEFAULT_TZ instead of discovering it on the first &week.
    const std::string default_tz_name = env_or("DEFAULT_TZ", "UTC");
    const std::chrono::time_zone* default_tz = nullptr;
    try {
        default_tz = fin::find_time_zone(default_tz_name);
    } catch (const std::exception& e) {
        std::cerr << "Could not load the timezone database: " << e.what() << "\n";
        return 1;
    }
    if (default_tz == nullptr) {
        std::cerr << "DEFAULT_TZ '" << default_tz_name << "' is not a known timezone (try America/Toronto).\n";
        return 1;
    }

    // A relative DB_PATH resolves against the folder the bot was started from,
    // so print the absolute path: running from a different folder would
    // otherwise silently create a second, empty database.
    std::unique_ptr<fin::Db> db;
    std::filesystem::path db_path;
    try {
        db_path = std::filesystem::absolute(env_or("DB_PATH", "data/fin-tracker.db"));
        std::filesystem::create_directories(db_path.parent_path());
        db = std::make_unique<fin::Db>(db_path.string());
    } catch (const std::exception& e) {
        std::cerr << "Could not open the database: " << e.what() << "\n";
        return 1;
    }
    std::cout << "Database: " << db_path.string() << "\n"
              << "Default timezone: " << default_tz->name() << "\n";

    // Message Content is a privileged intent: enable it in the Developer Portal
    // (Bot -> Message Content Intent) or Discord refuses the connection (4014).
    dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content);
    bot.on_log(dpp::utility::cout_logger());

    fin::CommandHandler handler(bot, *db, default_tz);
    bot.on_message_create([&handler](const dpp::message_create_t& event) { handler.on_message(event); });

    bot.on_ready([&bot](const dpp::ready_t&) {
        if (dpp::run_once<struct clear_slash_commands>()) {
            // The bot uses `&` messages only. Earlier builds registered a global
            // /ping command, which stays on Discord until removed explicitly.
            bot.global_bulk_command_delete();
        }
    });

    bot.start(dpp::st_wait);
    return 0;
}
