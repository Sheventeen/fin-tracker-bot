#include "money.hpp"

namespace fin {

std::string format_cents(std::int64_t cents) {
    // Work on the unsigned magnitude so INT64_MIN can't overflow.
    const std::uint64_t mag = cents < 0 ? 0 - static_cast<std::uint64_t>(cents)
                                        : static_cast<std::uint64_t>(cents);
    const std::string dollars = std::to_string(mag / 100);
    const std::uint64_t rem = mag % 100;

    std::string grouped;
    for (std::size_t i = 0; i < dollars.size(); ++i) {
        if (i != 0 && (dollars.size() - i) % 3 == 0) {
            grouped += ',';
        }
        grouped += dollars[i];
    }

    std::string out = "$" + grouped + ".";
    out += static_cast<char>('0' + rem / 10);
    out += static_cast<char>('0' + rem % 10);
    return out;
}

std::string format_signed_cents(std::int64_t cents) {
    if (cents == 0) {
        return format_cents(0);
    }
    return (cents < 0 ? "-" : "+") + format_cents(cents);
}

}  // namespace fin
