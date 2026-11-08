#include "periods.hpp"

#include <array>
#include <stdexcept>

#include "text.hpp"

namespace fin {

namespace chr = std::chrono;

namespace {

constexpr std::array<const char*, 7> kWeekdays{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr std::array<const char*, 12> kMonthsShort{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
constexpr std::array<const char*, 12> kMonthsLong{"January", "February", "March",     "April",
                                                  "May",     "June",     "July",      "August",
                                                  "September", "October", "November", "December"};

// Formatted by hand so output never depends on the process locale.
std::string format_date(chr::local_days day, bool with_year = true) {
    const chr::year_month_day ymd{day};
    const chr::weekday wd{day};
    std::string out = std::string(kWeekdays[wd.c_encoding()]) + ", " +
                      kMonthsShort[static_cast<unsigned>(ymd.month()) - 1] + " " +
                      std::to_string(static_cast<unsigned>(ymd.day()));
    if (with_year) {
        out += " " + std::to_string(static_cast<int>(ymd.year()));
    }
    return out;
}

std::int64_t to_utc_seconds(const chr::time_zone* tz, chr::local_days day) {
    // choose::earliest makes ambiguous or skipped local midnights (DST changes
    // at midnight in some zones) resolve deterministically instead of throwing.
    const chr::sys_seconds t = tz->to_sys(chr::local_seconds{day}, chr::choose::earliest);
    return t.time_since_epoch().count();
}

}  // namespace

PeriodRange compute_period(Period period, std::optional<int> year, const chr::time_zone* tz,
                           chr::sys_seconds now) {
    const chr::local_days today = chr::floor<chr::days>(tz->to_local(now));
    const chr::year_month_day ymd{today};

    chr::local_days start;
    chr::local_days end;
    std::string label;

    switch (period) {
        case Period::Today:
            start = today;
            end = today + chr::days{1};
            label = "Today · " + format_date(today);
            break;
        case Period::Week: {
            const chr::weekday wd{today};
            start = today - chr::days{wd.c_encoding()};  // back to Sunday
            end = start + chr::days{7};
            const chr::local_days last = end - chr::days{1};
            // Repeat the year only when the week spans New Year.
            const bool same_year = chr::year_month_day{start}.year() == chr::year_month_day{last}.year();
            label = "This week · " + format_date(start, !same_year) + " – " + format_date(last);
            break;
        }
        case Period::Month: {
            chr::year_month ym = ymd.year() / ymd.month();
            start = chr::local_days{ym / 1};
            ym += chr::months{1};
            end = chr::local_days{ym / 1};
            label = std::string(kMonthsLong[static_cast<unsigned>(ymd.month()) - 1]) + " " +
                    std::to_string(static_cast<int>(ymd.year()));
            break;
        }
        case Period::Year: {
            const chr::year y = year ? chr::year{*year} : ymd.year();
            start = chr::local_days{y / chr::January / 1};
            end = chr::local_days{(y + chr::years{1}) / chr::January / 1};
            label = "Year " + std::to_string(static_cast<int>(y));
            break;
        }
    }

    return PeriodRange{to_utc_seconds(tz, start), to_utc_seconds(tz, end), std::move(label)};
}

const chr::time_zone* find_time_zone(std::string_view name) {
    name = trim(name);
    if (name.empty()) {
        return nullptr;
    }
    const chr::tzdb& db = chr::get_tzdb();
    // Fast path: an exact name, which is what the database stores. Only typed
    // input in another case falls through to the scan below.
    try {
        return db.locate_zone(name);
    } catch (const std::runtime_error&) {
    }
    for (const chr::time_zone& zone : db.zones) {
        if (iequals_ascii(zone.name(), name)) {
            return &zone;
        }
    }
    for (const chr::time_zone_link& link : db.links) {
        if (iequals_ascii(link.name(), name)) {
            try {
                return chr::locate_zone(link.target());
            } catch (const std::runtime_error&) {
                return nullptr;
            }
        }
    }
    return nullptr;
}

std::string format_local_datetime(const chr::time_zone* tz, chr::sys_seconds t) {
    const chr::local_seconds local = tz->to_local(t);
    const chr::local_days day = chr::floor<chr::days>(local);
    const chr::hh_mm_ss<chr::seconds> hms{local - day};

    unsigned hour = static_cast<unsigned>(hms.hours().count());
    const char* suffix = hour < 12 ? "AM" : "PM";
    hour %= 12;
    if (hour == 0) {
        hour = 12;
    }
    const unsigned minute = static_cast<unsigned>(hms.minutes().count());
    return format_date(day) + " " + std::to_string(hour) + ":" + (minute < 10 ? "0" : "") +
           std::to_string(minute) + " " + suffix;
}

}  // namespace fin
