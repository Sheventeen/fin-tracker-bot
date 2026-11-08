// Amount parsing: valid amounts, decimals, and every AmountError.
// Run all test files with: ctest --test-dir build --output-on-failure

#include "check.hpp"
#include "parser.hpp"

namespace {

using fin::AmountError;

void test_amounts() {
    auto ok = [](const char* s, std::int64_t cents) {
        const fin::AmountResult r = fin::parse_amount_cents(s);
        return r.error == AmountError::None && r.cents == cents;
    };
    auto err = [](const char* s, AmountError e) { return fin::parse_amount_cents(s).error == e; };

    CHECK(ok("3.14", 314));
    CHECK(ok("3", 300));
    CHECK(ok("3.1", 310));
    CHECK(ok(".5", 50));
    CHECK(ok(".05", 5));
    CHECK(ok("0.01", 1));
    CHECK(ok("0010", 1000));
    CHECK(ok("10000000", 1'000'000'000));
    CHECK(ok("10000000.00", 1'000'000'000));

    CHECK(err("3.145", AmountError::TooManyDecimals));
    CHECK(err("3.100", AmountError::TooManyDecimals));
    CHECK(err("3.", AmountError::Invalid));
    CHECK(err(".", AmountError::Invalid));
    CHECK(err("", AmountError::Invalid));
    CHECK(err("1,000", AmountError::Invalid));
    CHECK(err("3.1.4", AmountError::Invalid));
    CHECK(err("3.14f", AmountError::Invalid));
    CHECK(err("$3", AmountError::Invalid));
    CHECK(err("0", AmountError::Zero));
    CHECK(err("0.00", AmountError::Zero));
    CHECK(err("10000000.01", AmountError::TooLarge));
    CHECK(err("99999999999999999999", AmountError::TooLarge));  // would overflow int64
}

}  // namespace

int main() {
    test_amounts();
    return test::finish();
}
