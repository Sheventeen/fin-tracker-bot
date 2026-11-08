#include "parser.hpp"

#include "text.hpp"

namespace fin {

namespace {

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool all_digits(std::string_view s) {
    for (char c : s) {
        if (!is_digit(c)) {
            return false;
        }
    }
    return true;
}

// Splits off the first whitespace-delimited token; `rest` keeps everything
// after it (untrimmed on the right, so notes keep their inner whitespace).
std::string_view next_token(std::string_view& rest) {
    while (!rest.empty() && is_space(rest.front())) {
        rest.remove_prefix(1);
    }
    std::size_t end = 0;
    while (end < rest.size() && !is_space(rest[end])) {
        ++end;
    }
    std::string_view token = rest.substr(0, end);
    rest.remove_prefix(end);
    return token;
}

// True for a token that is clearly meant as an amount: an optional `$`, `-` or
// `+`, then only digits, dots and commas, with at least one digit. "3.14",
// "$5", "-5" and "1,000" qualify (and get specific errors when invalid);
// chat like "&1st", "&...", "&-_-" or "&$$$" doesn't, so it is ignored.
bool looks_like_amount(std::string_view token) {
    if (!token.empty() && (token.front() == '$' || token.front() == '-' || token.front() == '+')) {
        token.remove_prefix(1);
    }
    bool has_digit = false;
    for (char c : token) {
        if (is_digit(c)) {
            has_digit = true;
        } else if (c != '.' && c != ',') {
            return false;
        }
    }
    return has_digit;
}

// "f, p, t, ..." from kCategories; `adding_only` keeps the money-in codes.
std::string code_list(bool adding_only, bool backticks) {
    std::string out;
    for (const Category& c : kCategories) {
        if (adding_only && c.sign() < 0) {
            continue;
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += backticks ? "`" + std::string(c.code) + "`" : std::string(c.code);
    }
    return out;
}

const std::string kExample = "Example: `&3.14 f omars restaurant`";
const std::string kTypeList = code_list(false, false);

Command parse_log(std::string_view amount_token, std::string_view rest) {
    const char first = amount_token.front();
    if (first == '$') {
        return ParseError{"Drop the `$` and write the number only. " + kExample};
    }
    if (first == '-' || first == '+') {
        return ParseError{
            "Amounts are always positive; the type sets the sign (" + code_list(true, true) +
            " add money, every other type subtracts). " + kExample};
    }

    const AmountResult amount = parse_amount_cents(amount_token);
    switch (amount.error) {
        case AmountError::None:
            break;
        case AmountError::Invalid:
            return ParseError{"That amount isn't a valid number. " + kExample};
        case AmountError::TooManyDecimals:
            return ParseError{"Amounts can have at most 2 decimal places, like `3.14`."};
        case AmountError::Zero:
            return ParseError{"The amount must be greater than 0."};
        case AmountError::TooLarge:
            return ParseError{"That amount is too large (the maximum is $10,000,000.00)."};
    }

    const std::string_view type_token = next_token(rest);
    if (type_token.empty()) {
        return ParseError{"Missing the type after the amount. " + kExample + ". Types: " + kTypeList +
                          " (see `&help`)."};
    }
    const Category* category = find_category(to_lower_ascii(type_token));
    if (category == nullptr) {
        return ParseError{"Unknown type. Use one of: " + kTypeList + " (see `&help`)."};
    }

    const std::string_view note = trim(rest);
    if (utf8_length(note) > kMaxNoteChars) {
        return ParseError{"That note is too long (the limit is " + std::to_string(kMaxNoteChars) +
                          " characters)."};
    }

    return LogCmd{category->sign() * amount.cents, category, std::string(note)};
}

Command parse_totals(Period period, std::string_view command_name, std::string_view rest) {
    TotalsCmd cmd{period, std::nullopt, false};
    const std::string usage = period == Period::Year
                                  ? "Usage: `&year [YYYY] [-m]`, for example `&year 2025 -m`."
                                  : "Usage: `&" + std::string(command_name) + " [-m]`.";

    for (std::string_view token = next_token(rest); !token.empty(); token = next_token(rest)) {
        if (iequals_ascii(token, "-m")) {
            cmd.more = true;
        } else if (period == Period::Year && token.size() == 4 && all_digits(token) && !cmd.year) {
            const int year = std::stoi(std::string(token));
            if (year < kMinYear || year > kMaxYear) {
                return ParseError{"The year must be between " + std::to_string(kMinYear) + " and " +
                                  std::to_string(kMaxYear) + "."};
            }
            cmd.year = year;
        } else {
            return ParseError{"I didn't understand that option. " + usage};
        }
    }
    return cmd;
}

}  // namespace

AmountResult parse_amount_cents(std::string_view s) {
    const std::size_t dot = s.find('.');
    std::string_view whole = s.substr(0, dot);
    const std::string_view frac = dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1);

    // Shape checks: digits, optionally a dot followed by 1+ digits. "3." and
    // "." are invalid, ".5" is fine.
    if (!all_digits(whole) || !all_digits(frac)) {
        return {0, AmountError::Invalid};
    }
    if (dot != std::string_view::npos && frac.empty()) {
        return {0, AmountError::Invalid};
    }
    if (whole.empty() && frac.empty()) {
        return {0, AmountError::Invalid};
    }
    if (frac.size() > 2) {
        return {0, AmountError::TooManyDecimals};
    }

    while (whole.size() > 1 && whole.front() == '0') {
        whole.remove_prefix(1);
    }
    // $10,000,000 has 8 whole digits; anything longer is too large, and the
    // length check keeps the accumulation below from overflowing.
    if (whole.size() > 8) {
        return {0, AmountError::TooLarge};
    }

    std::int64_t dollars = 0;
    for (char c : whole) {
        dollars = dollars * 10 + (c - '0');
    }
    std::int64_t cents_part = 0;
    if (!frac.empty()) {
        cents_part = (frac[0] - '0') * 10;
        if (frac.size() == 2) {
            cents_part += frac[1] - '0';
        }
    }

    const std::int64_t cents = dollars * 100 + cents_part;
    if (cents == 0) {
        return {0, AmountError::Zero};
    }
    if (cents > kMaxAmountCents) {
        return {0, AmountError::TooLarge};
    }
    return {cents, AmountError::None};
}

Command parse_message(std::string_view content) {
    content = trim(content);
    if (content.size() < 2 || content.front() != '&' || is_space(content[1])) {
        return Ignore{};
    }

    std::string_view rest = content.substr(1);
    const std::string_view first = next_token(rest);
    if (looks_like_amount(first)) {
        return parse_log(first, rest);
    }

    const std::string name = to_lower_ascii(first);
    if (name == "today") {
        return parse_totals(Period::Today, name, rest);
    }
    if (name == "week") {
        return parse_totals(Period::Week, name, rest);
    }
    if (name == "month") {
        return parse_totals(Period::Month, name, rest);
    }
    if (name == "year") {
        return parse_totals(Period::Year, name, rest);
    }
    if (name == "tz") {
        const std::string_view zone = next_token(rest);
        if (!trim(rest).empty()) {
            return ParseError{"Usage: `&tz` to see your timezone, or `&tz America/Toronto` to set it."};
        }
        if (zone.empty()) {
            return TzCmd{std::nullopt};
        }
        return TzCmd{std::string(zone)};
    }
    if (name == "undo") {
        if (!trim(rest).empty()) {
            return ParseError{"`&undo` takes no options; it removes your most recent entry."};
        }
        return UndoCmd{};
    }
    if (name == "help") {
        return HelpCmd{};
    }
    return Ignore{};
}

}  // namespace fin
