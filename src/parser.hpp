#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "categories.hpp"

namespace fin {

inline constexpr std::int64_t kMaxAmountCents = 1'000'000'000;  // $10,000,000.00
inline constexpr std::size_t kMaxNoteChars = 500;  // Unicode code points, not bytes
inline constexpr int kMinYear = 1970;
inline constexpr int kMaxYear = 9999;

enum class AmountError { None, Invalid, TooManyDecimals, Zero, TooLarge };

struct AmountResult {
    std::int64_t cents = 0;  // always positive when error == None
    AmountError error = AmountError::None;
};

// Parses "3.14", "3", ".5" into positive cents without touching floating point.
AmountResult parse_amount_cents(std::string_view s);

enum class Period { Today, Week, Month, Year };

// "&3.14 f omars restaurant"
struct LogCmd {
    std::int64_t amount_cents;  // signed: the category decides the sign
    const Category* category;
    std::string note;  // may be empty
};

// "&week -m", "&year 2025"
struct TotalsCmd {
    Period period;
    std::optional<int> year;  // only for Period::Year
    bool more = false;        // -m: per-type breakdown
};

// "&tz" (show) or "&tz America/Toronto" (set)
struct TzCmd {
    std::optional<std::string> zone;
};

struct UndoCmd {};
struct HelpCmd {};

// Something addressed to the bot that it can't accept. `message` never echoes
// user input, so it is always safe to post publicly.
struct ParseError {
    std::string message;
};

// Not a bot command (plain chat, "& then", unknown "&word").
struct Ignore {};

using Command = std::variant<Ignore, ParseError, LogCmd, TotalsCmd, TzCmd, UndoCmd, HelpCmd>;

Command parse_message(std::string_view content);

}  // namespace fin
