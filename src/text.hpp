#pragma once

#include <string>
#include <string_view>

namespace fin {

bool is_space(char c);
std::string_view trim(std::string_view s);
std::string to_lower_ascii(std::string_view s);
bool iequals_ascii(std::string_view a, std::string_view b);

// Number of Unicode code points in UTF-8 text (what users think of as characters,
// closely enough for a length limit).
std::size_t utf8_length(std::string_view s);

// Inserts a zero-width space after every '@' so user-supplied text echoed by
// the bot can never ping (@everyone, @here, <@id>, <@&role>).
std::string neutralize_mentions(std::string_view s);

}  // namespace fin
