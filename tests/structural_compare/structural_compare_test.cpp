// Standalone acceptance tests for nlohmann::structural_compare.
// No test framework required; compile and run directly, e.g.:
//   g++ -std=c++11 -I include tests/structural_compare/structural_compare_test.cpp -o structural_compare_test

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include <nlohmann/structural_compare.hpp>

using nlohmann::json;

static int failures = 0;
static int checks = 0;

static void check(bool condition, const std::string& name)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

static void expect_equal(const json& a, const json& b, const std::string& name)
{
    const auto res = nlohmann::structural_compare(a, b);
    check(res.equal, name + " (expected equal)");
    if (!res.equal)
    {
        std::cerr << "  first_difference: \"" << res.first_difference.to_string() << "\"\n";
    }
}

static void expect_diff(const json& a, const json& b, const std::string& pointer,
                        const std::string& name)
{
    const auto res = nlohmann::structural_compare(a, b);
    check(!res.equal, name + " (expected different)");
    check(res.first_difference.to_string() == pointer,
          name + " (expected pointer \"" + pointer + "\", got \"" +
          res.first_difference.to_string() + "\")");
}

int main()
{
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::int64_t i64min = (std::numeric_limits<std::int64_t>::min)();
    const std::int64_t i64max = (std::numeric_limits<std::int64_t>::max)();
    const std::uint64_t u64max = (std::numeric_limits<std::uint64_t>::max)();

    // ---- acceptance: integers beyond 2^53 must not be conflated ----------
    expect_diff(json(9007199254740992LL), json(9007199254740993LL), "",
                "2^53 vs 2^53+1 as integers");
    expect_diff(json(9007199254740993LL), json(9007199254740992.0), "",
                "2^53+1 integer vs 2^53 double (would be equal after double conversion)");
    expect_equal(json(9007199254740992LL), json(9007199254740992.0),
                 "2^53 integer vs exactly representable double");
    expect_equal(json(9007199254740993LL), json(9007199254740993LL),
                 "same large integer");

    // ---- acceptance: integer 1 equals float 1.0, string "1" does not -----
    expect_equal(json(1), json(1.0), "integer 1 vs float 1.0");
    expect_equal(json(1u), json(1.0), "unsigned 1 vs float 1.0");
    expect_diff(json(1), json(1.5), "", "integer 1 vs float 1.5");
    expect_diff(json("1"), json(1), "", "string 1 vs integer 1 (no coercion)");
    expect_diff(json("1"), json(1.0), "", "string 1 vs float 1.0 (no coercion)");
    expect_diff(json("1.0"), json(1.0), "", "string 1.0 vs float 1.0 (no coercion)");

    // ---- signed/unsigned boundaries: no overflow, no wraparound ----------
    expect_diff(json(-1), json(u64max), "", "-1 vs uint64 max (no wraparound)");
    expect_diff(json(i64min), json(u64max), "", "int64 min vs uint64 max");
    expect_diff(json(i64min), json(u64max - 1), "", "int64 min vs uint64 max-1");
    expect_equal(json(i64max), json(static_cast<std::uint64_t>(i64max)),
                 "int64 max vs same value as unsigned");
    expect_equal(json(0), json(0u), "signed 0 vs unsigned 0");
    expect_diff(json(-1), json(0u), "", "-1 vs unsigned 0");
    // uint64 max vs the double 2^64 (18446744073709551615.0 rounds up):
    // must be different, and the comparison must not overflow
    expect_diff(json(u64max), json(18446744073709551615.0), "",
                "uint64 max vs double 2^64 (no overflow)");
    expect_equal(json(static_cast<std::uint64_t>(9223372036854775808ULL)),
                 json(9223372036854775808.0),
                 "2^63 unsigned vs exactly representable double");
    expect_equal(json(i64min), json(-9223372036854775808.0),
                 "int64 min vs exactly representable double");
    expect_diff(json(i64max), json(9223372036854775807.0), "",
                "int64 max vs double that rounds to 2^63");
    expect_diff(json(-1), json(18446744073709551615.0), "", "-1 vs double 2^64");

    // ---- -0.0 policy: -0.0 == 0.0 == integer 0 ---------------------------
    expect_equal(json(-0.0), json(0.0), "-0.0 vs 0.0");
    expect_equal(json(-0.0), json(0), "-0.0 vs integer 0");
    expect_equal(json(-0.0), json(0u), "-0.0 vs unsigned 0");

    // ---- infinity policy --------------------------------------------------
    expect_equal(json(inf), json(inf), "+inf vs +inf");
    expect_equal(json(-inf), json(-inf), "-inf vs -inf");
    expect_diff(json(inf), json(-inf), "", "+inf vs -inf");
    expect_diff(json(inf), json((std::numeric_limits<double>::max)()), "",
                "+inf vs max finite double");
    expect_diff(json(inf), json(i64max), "", "+inf vs int64 max");
    expect_diff(json(-inf), json(i64min), "", "-inf vs int64 min");

    // ---- NaN policy: rejected, never equal to anything --------------------
    expect_diff(json(nan), json(nan), "", "NaN vs NaN (rejected)");
    expect_diff(json(nan), json(0.0), "", "NaN vs 0.0");
    expect_diff(json(nan), json(1), "", "NaN vs integer 1");
    expect_diff(json(nan), json(inf), "", "NaN vs +inf");

    // ---- objects: key order insignificant ---------------------------------
    expect_equal(json::parse(R"({"a":1,"b":[1,2],"c":{"x":true}})"),
                 json::parse(R"({"c":{"x":true},"b":[1,2],"a":1})"),
                 "object key order insignificant");
    expect_diff(json::parse(R"({"a":1,"b":2})"), json::parse(R"({"a":1})"), "",
                "object size mismatch");
    expect_diff(json::parse(R"({"x":1})"), json::parse(R"({"y":1})"), "/x",
                "missing key pointer");
    expect_diff(json::parse(R"({"a":{"b":1}})"), json::parse(R"({"a":{"b":2}})"),
                "/a/b", "nested object pointer");

    // ---- arrays: element order significant --------------------------------
    expect_equal(json::parse("[1,2,3]"), json::parse("[1,2,3]"), "same array");
    expect_diff(json::parse("[1,2]"), json::parse("[2,1]"), "/0",
                "array order significant");
    expect_diff(json::parse("[1,2,3]"), json::parse("[1,2,4]"), "/2",
                "array last element differs");
    expect_diff(json::parse("[1,2]"), json::parse("[1,2,3]"), "",
                "array size mismatch");
    expect_diff(json::parse("[[[0]]]"), json::parse("[[[1]]]"), "/0/0/0",
                "deeply nested array pointer");
    expect_diff(json::parse(R"([{"k":[1,{"j":2}]}])"),
                json::parse(R"([{"k":[1,{"j":3}]}])"),
                "/0/k/1/j", "mixed nesting pointer");

    // ---- nested difference pointer with RFC 6901 escaping -----------------
    expect_diff(json::parse(R"({"a/b":{"~key":[1,2,3]}})"),
                json::parse(R"({"a/b":{"~key":[1,2,4]}})"),
                "/a~1b/~0key/2", "escaped pointer for nested difference");

    // ---- other types -------------------------------------------------------
    expect_equal(json(nullptr), json(nullptr), "null vs null");
    expect_diff(json(nullptr), json(0), "", "null vs integer 0");
    expect_diff(json(true), json(1), "", "boolean true vs integer 1 (no coercion)");
    expect_equal(json("abc"), json("abc"), "equal strings");
    expect_diff(json("abc"), json("abd"), "", "different strings");
    expect_equal(json::parse(R"({"s":"x/y~z","n":-7,"f":2.5})"),
                 json::parse(R"({"f":2.5,"n":-7,"s":"x/y~z"})"),
                 "mixed document, reordered keys");

    // ---- read-only: operands are not modified ------------------------------
    {
        const json a = json::parse(R"({"big":9007199254740993,"arr":[1,{"x":2.5}]})");
        const json b = json::parse(R"({"arr":[1,{"x":2.5}],"big":9007199254740993})");
        const json a_copy = a;
        const json b_copy = b;
        const auto res = nlohmann::structural_compare(a, b);
        check(res.equal, "read-only check compares equal");
        check(a == a_copy && b == b_copy, "operands unchanged after comparison");
    }

    // ---- json::operator== semantics remain unchanged -----------------------
    check(json(1) == json(1.0), "operator== still equates 1 and 1.0 (library semantics)");
    check(!(json(9007199254740992LL) == json(9007199254740993LL)),
          "operator== still distinguishes large integers");
    check(json::parse("[1,2]") == json::parse("[1,2]"), "operator== on arrays unchanged");

    // ---- convenience wrapper ------------------------------------------------
    check(nlohmann::structural_equal(json(1), json(1.0)), "structural_equal(1, 1.0)");
    check(!nlohmann::structural_equal(json(1), json("1")), "structural_equal rejects string");

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
