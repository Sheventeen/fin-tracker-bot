#pragma once

#include <array>
#include <string_view>

namespace fin {

// Which summary line a category's entries count toward. The kind also decides
// the sign: spending subtracts, income and investments add.
enum class CategoryKind { Spending, Income, Investment };

// The single source of truth for expense types. The `categories` table in the
// database is synced from this list on every startup.
struct Category {
    std::string_view code;  // what users type: "f", "inv", ...
    std::string_view name;  // what the bot shows: "Food", "Investment", ...
    CategoryKind kind;

    // -1 = money out, +1 = money in.
    constexpr int sign() const { return kind == CategoryKind::Spending ? -1 : +1; }
};

inline constexpr std::array<Category, 8> kCategories{{
    {"f", "Food", CategoryKind::Spending},
    {"p", "Personal", CategoryKind::Spending},
    {"t", "Transportation", CategoryKind::Spending},
    {"d", "Dependant/Pet", CategoryKind::Spending},
    {"b", "Bill", CategoryKind::Spending},
    {"s", "Self improvement", CategoryKind::Spending},
    {"i", "Income", CategoryKind::Income},
    {"inv", "Investment", CategoryKind::Investment},
}};

// `code` must already be lowercase. Returns nullptr for unknown codes.
constexpr const Category* find_category(std::string_view code) {
    for (const Category& c : kCategories) {
        if (c.code == code) {
            return &c;
        }
    }
    return nullptr;
}

}  // namespace fin
