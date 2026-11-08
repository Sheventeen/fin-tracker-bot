#pragma once

#include <dpp/dpp.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "db.hpp"
#include "parser.hpp"

namespace fin {

// Turns `&` messages into database actions and Discord replies.
//
// Reply rules:
//  - Logging an expense: react ✅ (falls back to a text reply if the bot can't react).
//  - Anything containing the user's data (totals, &undo, &tz): in a DM, reply
//    there; in a server, send it by DM and react ✅ on the command, so nothing
//    private is posted in the channel.
//  - &help and parse errors: reply in the channel (they contain nothing private).
class CommandHandler {
public:
    CommandHandler(dpp::cluster& bot, Db& db, const std::chrono::time_zone* default_tz);

    void on_message(const dpp::message_create_t& event);

private:
    void handle(const dpp::message& msg, const LogCmd& cmd);
    void handle(const dpp::message& msg, const TotalsCmd& cmd);
    void handle(const dpp::message& msg, const TzCmd& cmd);
    void handle(const dpp::message& msg, const UndoCmd& cmd);
    void handle(const dpp::message& msg, const HelpCmd& cmd);
    void handle(const dpp::message& msg, const ParseError& cmd);
    void handle(const dpp::message&, const Ignore&) {}

    // Runs `fn`, replying with a generic error in the channel if it throws.
    void run_guarded(const dpp::message& msg, const std::function<void()>& fn);
    void undo_now(const dpp::message& msg);

    // A user's timezone, read from the database once per command.
    struct UserZone {
        const std::chrono::time_zone* tz;   // never null
        std::optional<std::string> stored;  // the name the user saved, if any
        bool stored_invalid = false;        // saved, but gone from the installed tzdata

        bool using_default() const { return !stored || stored_invalid; }
    };
    UserZone user_zone(dpp::snowflake user_id);

    // Posts `text` in the message's channel as a reply to it.
    void reply_in_channel(const dpp::message& msg, const std::string& text);
    // Adds ✅ to the message; if that fails, replies with `fallback_text` instead.
    void react_ok(const dpp::message& msg, const std::string& fallback_text);
    // Sends `text` privately: a normal reply in DMs, a DM (+ ✅ in the channel) in servers.
    void reply_private(const dpp::message& msg, const std::string& text);

    dpp::cluster& bot_;
    Db& db_;
    const std::chrono::time_zone* default_tz_;
};

// Builds the &help text from the category table.
std::string help_text();

}  // namespace fin
