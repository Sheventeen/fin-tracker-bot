#include "commands.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <variant>

#include "categories.hpp"
#include "money.hpp"
#include "periods.hpp"
#include "text.hpp"

namespace fin {

namespace chr = std::chrono;

namespace {

constexpr const char* kCheckMark = "\xE2\x9C\x85";  // ✅

// How long `&undo` waits before acting. DPP runs event handlers on a thread
// pool that doesn't keep arrival order, so `&5 f lunch` followed quickly by
// `&undo` (or both replayed after a reconnect) could otherwise run undo first
// and remove an older entry. DPP timers count whole seconds, so 2 means the
// undo runs 1 to 2 seconds later.
constexpr std::uint64_t kUndoDelaySeconds = 2;

// Every message the bot sends goes through here: no @everyone, role, user or
// reply pings, ever. `channel_id` may be empty for a DM, whose channel
// direct_message_create fills in.
dpp::message make_message(dpp::snowflake channel_id, const std::string& text) {
    dpp::message m(channel_id, text);
    m.set_allowed_mentions(false, false, false, false);
    return m;
}

// Callbacks run after the handler returns, so they capture this small copy
// of the fields needed to reply or react, not the whole event message.
dpp::message reply_target(const dpp::message& msg) {
    dpp::message target;
    target.id = msg.id;
    target.channel_id = msg.channel_id;
    target.guild_id = msg.guild_id;
    return target;
}

// For user-supplied text echoed back (notes): no markdown tricks, no pings.
std::string safe_user_text(const std::string& text) {
    return neutralize_mentions(dpp::utility::markdown_escape(text));
}

// Rows may use a code that was later removed from kCategories (the database
// keeps it so old entries stay valid); show the raw code for those.
std::string category_name(const std::string& code) {
    const Category* c = find_category(code);
    return c != nullptr ? std::string(c->name) : code;
}

std::string entry_count(std::int64_t count) {
    return std::to_string(count) + (count == 1 ? " entry" : " entries");
}

chr::sys_seconds now_seconds() {
    return chr::floor<chr::seconds>(chr::system_clock::now());
}

std::string format_totals(const PeriodRange& range, const chr::time_zone* tz,
                          const std::vector<Db::CategoryTotal>& rows, bool more) {
    std::string out = "**" + range.label + "** (" + std::string(tz->name()) + ")\n";
    if (rows.empty()) {
        return out + "Nothing logged in this period.";
    }

    // Every row lands in exactly one bucket, so the lines always add up to Net.
    std::int64_t spent = 0;     // CategoryKind::Spending (negative)
    std::int64_t income = 0;    // CategoryKind::Income
    std::int64_t invested = 0;  // CategoryKind::Investment
    std::int64_t other = 0;     // codes no longer in kCategories
    std::int64_t net = 0;
    for (const Db::CategoryTotal& row : rows) {
        net += row.sum_cents;
        const Category* c = find_category(row.category);
        if (c == nullptr) {
            other += row.sum_cents;
            continue;
        }
        switch (c->kind) {
            case CategoryKind::Spending:
                spent += row.sum_cents;
                break;
            case CategoryKind::Income:
                income += row.sum_cents;
                break;
            case CategoryKind::Investment:
                invested += row.sum_cents;
                break;
        }
    }

    out += "Spent: " + format_cents(spent) + "\n";
    out += "Income: " + format_cents(income) + "\n";
    out += "Invested: " + format_cents(invested) + "\n";
    if (other != 0) {
        out += "Other (retired types): " + format_signed_cents(other) + "\n";
    }
    out += "Net: " + format_signed_cents(net);

    if (more) {
        out += "\n\n**By type**";
        // Category-table order keeps the breakdown predictable; retired codes go last.
        for (const Category& c : kCategories) {
            for (const Db::CategoryTotal& row : rows) {
                if (row.category == c.code) {
                    out += "\n" + std::string(c.name) + ": " + format_signed_cents(row.sum_cents) + " (" +
                           entry_count(row.count) + ")";
                }
            }
        }
        for (const Db::CategoryTotal& row : rows) {
            if (find_category(row.category) == nullptr) {
                out += "\n" + safe_user_text(row.category) + ": " + format_signed_cents(row.sum_cents) + " (" +
                       entry_count(row.count) + ")";
            }
        }
    }
    return out;
}

}  // namespace

std::string help_text() {
    std::string out =
        "**fin-tracker**: log money with `&<amount> <type> [note]`\n"
        "Example: `&3.14 f omars restaurant`\n\n"
        "**Types**\n";
    std::string minus;
    std::string plus;
    for (const Category& c : kCategories) {
        std::string& line = c.sign() < 0 ? minus : plus;
        if (!line.empty()) {
            line += " · ";
        }
        line += "`" + std::string(c.code) + "` " + std::string(c.name);
    }
    out += "Subtract: " + minus + "\n";
    out += "Add: " + plus + "\n\n";
    out +=
        "**Commands**\n"
        "`&today` `&week` `&month` `&year [YYYY]`: totals (add `-m` for a per-type breakdown)\n"
        "`&undo`: remove your most recent entry\n"
        "`&tz [zone]`: show or set your timezone, e.g. `&tz America/Toronto`\n"
        "`&help`: this message\n\n"
        "Your entries belong to you, not the server: they follow you to every server and DM. "
        "In servers, totals and other personal replies are sent to your DMs.";
    return out;
}

CommandHandler::CommandHandler(dpp::cluster& bot, Db& db, const chr::time_zone* default_tz)
    : bot_(bot), db_(db), default_tz_(default_tz) {}

void CommandHandler::on_message(const dpp::message_create_t& event) {
    const dpp::message& msg = event.msg;
    if (msg.author.is_bot() || !msg.webhook_id.empty()) {
        return;
    }
    run_guarded(msg, [&] {
        const Command cmd = parse_message(msg.content);
        std::visit([&](const auto& c) { handle(msg, c); }, cmd);
    });
}

void CommandHandler::run_guarded(const dpp::message& msg, const std::function<void()>& fn) {
    try {
        fn();
    } catch (const std::exception& e) {
        bot_.log(dpp::ll_error, "command failed for user " + msg.author.id.str() + ": " + e.what());
        reply_in_channel(msg, "Something went wrong on my side; please try again.");
    }
}

void CommandHandler::handle(const dpp::message& msg, const LogCmd& cmd) {
    // Use Discord's timestamp for the message, not "now": events replayed
    // after a reconnect still land on the day they were sent.
    const std::int64_t created_at =
        msg.sent != 0 ? static_cast<std::int64_t>(msg.sent) : now_seconds().time_since_epoch().count();

    Db::NewTransaction tx{
        msg.author.id,
        cmd.amount_cents,
        std::string(cmd.category->code),
        cmd.note,
        created_at,
        msg.guild_id.empty() ? std::nullopt : std::optional<std::uint64_t>(msg.guild_id),
        std::optional<std::uint64_t>(msg.id),
    };
    db_.add_transaction(tx);  // a duplicate (replayed event) is already stored: still confirm

    react_ok(msg, "Logged " + format_signed_cents(cmd.amount_cents) + " " + std::string(cmd.category->name) + ".");
}

void CommandHandler::handle(const dpp::message& msg, const TotalsCmd& cmd) {
    const chr::time_zone* tz = user_zone(msg.author.id).tz;
    const PeriodRange range = compute_period(cmd.period, cmd.year, tz, now_seconds());
    const auto rows = db_.totals_by_category(msg.author.id, range.start_utc, range.end_utc);
    reply_private(msg, format_totals(range, tz, rows, cmd.more));
}

void CommandHandler::handle(const dpp::message& msg, const TzCmd& cmd) {
    if (!cmd.zone) {
        const UserZone zone = user_zone(msg.author.id);
        const std::string name = std::string(zone.tz->name());
        const std::string local = format_local_datetime(zone.tz, now_seconds());
        if (!zone.using_default()) {
            reply_private(msg, "Your timezone is **" + name + "** (local time: " + local + ").");
        } else if (zone.stored) {
            reply_private(msg, "Your saved timezone **" + safe_user_text(*zone.stored) +
                                   "** is no longer recognized, so I'm using the default **" + name +
                                   "** (local time: " + local + "). Set yours again with `&tz America/Toronto`.");
        } else {
            reply_private(msg, "You haven't set a timezone, so I'm using the default **" + name +
                                   "** (local time: " + local + "). Set yours with `&tz America/Toronto`.");
        }
        return;
    }

    const chr::time_zone* tz = find_time_zone(*cmd.zone);
    if (tz == nullptr) {
        // Doesn't echo the input, so it is safe to post publicly.
        reply_in_channel(msg,
                         "I don't know that timezone. Use a name like `America/Toronto` or `Europe/London` "
                         "(full list: <https://en.wikipedia.org/wiki/List_of_tz_database_time_zones>).");
        return;
    }
    const chr::sys_seconds now = now_seconds();
    db_.set_timezone(msg.author.id, tz->name(), now.time_since_epoch().count());
    reply_private(msg, "Timezone set to **" + std::string(tz->name()) + "**. Your local time is " +
                           format_local_datetime(tz, now) + "; if that's wrong, pick another zone.");
}

void CommandHandler::handle(const dpp::message& msg, const UndoCmd&) {
    // Wait out entries sent just before this `&undo` that may still be queued
    // (see kUndoDelaySeconds). A DPP timer repeats until stopped, so `fired`
    // guards against a second tick sneaking in before stop_timer takes effect.
    auto fired = std::make_shared<std::atomic<bool>>(false);
    bot_.start_timer(
        [this, msg, fired](dpp::timer t) {
            bot_.stop_timer(t);
            if (!fired->exchange(true)) {
                run_guarded(msg, [&] { undo_now(msg); });
            }
        },
        kUndoDelaySeconds);
}

void CommandHandler::undo_now(const dpp::message& msg) {
    // Only entries sent before this `&undo`: one sent after it, but handled
    // first, is never removed.
    const std::optional<Db::RemovedTransaction> removed = db_.undo_last(msg.author.id, msg.id);
    if (!removed) {
        reply_private(msg, "Nothing to undo: you have no entries.");
        return;
    }
    std::string text = "Removed " + format_signed_cents(removed->amount_cents) + " " +
                       category_name(removed->category);
    if (!removed->note.empty()) {
        text += " (" + safe_user_text(removed->note) + ")";
    }
    const chr::time_zone* tz = user_zone(msg.author.id).tz;
    text += ", logged " + format_local_datetime(tz, chr::sys_seconds{chr::seconds{removed->created_at}}) + ".";
    reply_private(msg, text);
}

void CommandHandler::handle(const dpp::message& msg, const HelpCmd&) {
    reply_in_channel(msg, help_text());
}

void CommandHandler::handle(const dpp::message& msg, const ParseError& cmd) {
    reply_in_channel(msg, cmd.message);
}

CommandHandler::UserZone CommandHandler::user_zone(dpp::snowflake user_id) {
    UserZone zone{default_tz_, db_.get_timezone(user_id)};
    if (zone.stored) {
        if (const chr::time_zone* tz = find_time_zone(*zone.stored)) {
            zone.tz = tz;
        } else {
            // A zone removed from a future tzdata release: fall back rather than fail.
            bot_.log(dpp::ll_warning, "stored timezone '" + *zone.stored + "' not found; using default");
            zone.stored_invalid = true;
        }
    }
    return zone;
}

void CommandHandler::reply_in_channel(const dpp::message& msg, const std::string& text) {
    dpp::message reply = make_message(msg.channel_id, text);
    reply.guild_id = msg.guild_id;
    reply.set_reference(msg.id, msg.guild_id, msg.channel_id, false);  // still sends if the original was deleted
    bot_.message_create(reply, [this](const dpp::confirmation_callback_t& cc) {
        if (cc.is_error()) {
            bot_.log(dpp::ll_warning, "could not send reply: " + cc.get_error().message);
        }
    });
}

void CommandHandler::react_ok(const dpp::message& msg, const std::string& fallback_text) {
    const dpp::message target = reply_target(msg);
    bot_.message_add_reaction(msg.id, msg.channel_id, kCheckMark,
                              [this, target, fallback_text](const dpp::confirmation_callback_t& cc) {
                                  if (cc.is_error()) {
                                      bot_.log(dpp::ll_warning,
                                               "could not add reaction (missing Add Reactions / Read Message "
                                               "History?): " + cc.get_error().message);
                                      reply_in_channel(target, fallback_text);
                                  }
                              });
}

void CommandHandler::reply_private(const dpp::message& msg, const std::string& text) {
    if (msg.guild_id.empty()) {
        reply_in_channel(msg, text);  // already a DM
        return;
    }
    const dpp::message target = reply_target(msg);
    bot_.direct_message_create(msg.author.id, make_message({}, text), [this, target](const dpp::confirmation_callback_t& cc) {
        if (cc.is_error()) {
            bot_.log(dpp::ll_info, "could not DM user: " + cc.get_error().message);
            reply_in_channel(target,
                             "I couldn't DM you, so I didn't post your info here. Allow DMs from server "
                             "members (Privacy Settings for this server), or message me directly.");
            return;
        }
        react_ok(target, "Sent you a DM.");
    });
}

}  // namespace fin
