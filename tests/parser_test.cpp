// Message grammar: parse_message for every command and its errors.
// Run all test files with: ctest --test-dir build --output-on-failure

#include <string>
#include <variant>

#include "check.hpp"
#include "parser.hpp"

namespace {

using fin::AmountError;

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
    {
        // Commas get their own message instead of the generic "not a valid number".
        const auto c = fin::parse_message("&1,000 b rent");  // kept alive: `e` points into it
        const auto* e = as<fin::ParseError>(c);
        CHECK(e != nullptr && e->message.find("commas") != std::string::npos);
    }
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

}  // namespace

int main() {
    test_parser();
    return test::finish();
}
