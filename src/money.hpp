#pragma once

#include <cstdint>
#include <string>

namespace fin {

// 123456 -> "$1,234.56"; the sign is dropped (magnitude only).
std::string format_cents(std::int64_t cents);

// -314 -> "-$3.14", 5000 -> "+$50.00", 0 -> "$0.00".
std::string format_signed_cents(std::int64_t cents);

}  // namespace fin
