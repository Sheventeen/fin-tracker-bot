// Money formatting and the text helpers.
// Run all test files with: ctest --test-dir build --output-on-failure

#include "check.hpp"
#include "money.hpp"
#include "text.hpp"

namespace {

void test_money_and_text() {
    CHECK(fin::format_cents(123456) == "$1,234.56");
    CHECK(fin::format_cents(-314) == "$3.14");
    CHECK(fin::format_cents(5) == "$0.05");
    CHECK(fin::format_cents(100000) == "$1,000.00");
    CHECK(fin::format_cents(99999) == "$999.99");
    CHECK(fin::format_cents(1'000'000'000) == "$10,000,000.00");
    CHECK(fin::format_signed_cents(-314) == "-$3.14");
    CHECK(fin::format_signed_cents(5000) == "+$50.00");
    CHECK(fin::format_signed_cents(0) == "$0.00");

    CHECK(fin::neutralize_mentions("@everyone") == "@\xE2\x80\x8B" "everyone");
    CHECK(fin::neutralize_mentions("<@123> hi") == "<@\xE2\x80\x8B" "123> hi");
    CHECK(fin::neutralize_mentions("no mentions") == "no mentions");
    CHECK(fin::trim("  a b \n") == "a b");
    CHECK(fin::iequals_ascii("INV", "inv"));
    CHECK(fin::utf8_length("") == 0);
    CHECK(fin::utf8_length("abc") == 3);
    CHECK(fin::utf8_length("caf\xC3\xA9") == 4);         // é is 2 bytes
    CHECK(fin::utf8_length("\xF0\x9F\x8D\x95!") == 2);  // emoji is 4 bytes
}

}  // namespace

int main() {
    test_money_and_text();
    return test::finish();
}
