#include "text.hpp"

#include <algorithm>

namespace fin {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

namespace {
char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
}  // namespace

std::string to_lower_ascii(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), lower_ascii);
    return out;
}

bool iequals_ascii(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](char x, char y) { return lower_ascii(x) == lower_ascii(y); });
}

std::size_t utf8_length(std::string_view s) {
    // Count every byte except continuation bytes (10xxxxxx).
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

std::string neutralize_mentions(std::string_view s) {
    static constexpr std::string_view kZeroWidthSpace = "\xE2\x80\x8B";  // U+200B
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out += c;
        if (c == '@') {
            out += kZeroWidthSpace;
        }
    }
    return out;
}

}  // namespace fin
