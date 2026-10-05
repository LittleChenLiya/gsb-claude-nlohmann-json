//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

// This file tests the explicit diff tool for ordered_json defined in
// <nlohmann/ordered_diff.hpp>. Value changes and object member order
// changes are reported separately. Nested objects are compared by their
// actual member order; arrays are positional. Numbers are compared along
// the library's original type. The ignore_order option hides order
// differences but not value changes.

#include "doctest_compatibility.h"

#include <nlohmann/json.hpp>
#include <nlohmann/ordered_diff.hpp>

#include <string>
#include <vector>

using nlohmann::ordered_diff;
using nlohmann::ordered_diff_result;
using nlohmann::ordered_json;

TEST_CASE("ordered_diff")
{
    SECTION("identical values produce no differences")
    {
        ordered_json a = {{"a", 1}, {"b", 2}};
        ordered_json b = {{"a", 1}, {"b", 2}};

        ordered_diff_result result = ordered_diff(a, b);

        CHECK_FALSE(result.has_value_changes());
        CHECK_FALSE(result.has_order_changes());
        CHECK(result.value_changes.empty());
        CHECK(result.order_changes.empty());
    }

    SECTION("same members in different order produce only order changes")
    {
        ordered_json a = {{"a", 1}, {"b", 2}};
        ordered_json b = {{"b", 2}, {"a", 1}};

        ordered_diff_result result = ordered_diff(a, b);

        // No value changes: the same members with the same values.
        CHECK_FALSE(result.has_value_changes());
        CHECK(result.value_changes.empty());

        // Order change reported using member names, not iterators.
        CHECK(result.has_order_changes());
        REQUIRE(result.order_changes.size() == 1);
        CHECK(result.order_changes[0] ==
              "$: member order changed from [a, b] to [b, a]");
    }

    SECTION("changing a value produces both value and order changes")
    {
        ordered_json a = {{"a", 1}, {"b", 2}};
        ordered_json b = {{"b", 3}, {"a", 1}};

        ordered_diff_result result = ordered_diff(a, b);

        // b's value changed from 2 to 3.
        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$.b: number value changed");

        // Order also changed.
        CHECK(result.has_order_changes());
        REQUIRE(result.order_changes.size() == 1);
        CHECK(result.order_changes[0] ==
              "$: member order changed from [a, b] to [b, a]");
    }

    SECTION("nested objects are compared by actual member order")
    {
        ordered_json a = {
            {"outer", {{"x", 1}, {"y", 2}}},
            {"z", 3}
        };
        ordered_json b = {
            {"outer", {{"y", 2}, {"x", 1}}},
            {"z", 3}
        };

        ordered_diff_result result = ordered_diff(a, b);

        // No value changes.
        CHECK_FALSE(result.has_value_changes());

        // The nested object's order change is reported at the correct path.
        CHECK(result.has_order_changes());
        REQUIRE(result.order_changes.size() == 1);
        CHECK(result.order_changes[0] ==
              "$.outer: member order changed from [x, y] to [y, x]");
    }

    SECTION("nested objects with value and order changes")
    {
        ordered_json a = {
            {"outer", {{"x", 1}, {"y", 2}}}
        };
        ordered_json b = {
            {"outer", {{"y", 9}, {"x", 1}}}
        };

        ordered_diff_result result = ordered_diff(a, b);

        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$.outer.y: number value changed");

        CHECK(result.has_order_changes());
        REQUIRE(result.order_changes.size() == 1);
        CHECK(result.order_changes[0] ==
              "$.outer: member order changed from [x, y] to [y, x]");
    }

    SECTION("arrays are positional")
    {
        ordered_json a = {1, 2, 3};
        ordered_json b = {1, 2, 4};

        ordered_diff_result result = ordered_diff(a, b);

        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$[2]: number value changed");

        // Arrays have no member order, so no order changes.
        CHECK_FALSE(result.has_order_changes());
    }

    SECTION("arrays with different sizes")
    {
        ordered_json a = {1, 2, 3};
        ordered_json b = {1, 2};

        ordered_diff_result result = ordered_diff(a, b);

        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$: array size mismatch (3 vs 2)");

        CHECK_FALSE(result.has_order_changes());
    }

    SECTION("numbers are compared along the library's original type")
    {
        // integer vs float: type mismatch, even if numerically equal.
        ordered_json a = 1;
        ordered_json b = 1.0;

        ordered_diff_result result = ordered_diff(a, b);

        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$: type mismatch (number vs number)");

        // integer vs integer: equal.
        ordered_json c = 1;
        ordered_json d = 1;
        ordered_diff_result result2 = ordered_diff(c, d);
        CHECK_FALSE(result2.has_value_changes());

        // integer vs integer: value changed.
        ordered_json e = 1;
        ordered_json f = 2;
        ordered_diff_result result3 = ordered_diff(e, f);
        CHECK(result3.has_value_changes());
        REQUIRE(result3.value_changes.size() == 1);
        CHECK(result3.value_changes[0] == "$: number value changed");
    }

    SECTION("type mismatch between object and array")
    {
        ordered_json a = {{"a", 1}};
        ordered_json b = {1};

        ordered_diff_result result = ordered_diff(a, b);

        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$: type mismatch (object vs array)");

        CHECK_FALSE(result.has_order_changes());
    }

    SECTION("added members are reported as value changes")
    {
        ordered_json a = {{"a", 1}};
        ordered_json b = {{"a", 1}, {"b", 2}};

        ordered_diff_result result = ordered_diff(a, b);

        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$: object members changed (added: b)");

        CHECK_FALSE(result.has_order_changes());
    }

    SECTION("removed members are reported as value changes")
    {
        ordered_json a = {{"a", 1}, {"b", 2}};
        ordered_json b = {{"a", 1}};

        ordered_diff_result result = ordered_diff(a, b);

        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$: object members changed (removed: b)");

        CHECK_FALSE(result.has_order_changes());
    }

    SECTION("ignore_order hides order changes but not value changes")
    {
        ordered_json a = {{"a", 1}, {"b", 2}};
        ordered_json b = {{"b", 3}, {"a", 1}};

        ordered_diff_result result = ordered_diff(a, b, /* ignore_order = */ true);

        // Value change is still reported.
        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$.b: number value changed");

        // Order change is hidden.
        CHECK_FALSE(result.has_order_changes());
    }

    SECTION("ignore_order with only order changes produces no differences")
    {
        ordered_json a = {{"a", 1}, {"b", 2}};
        ordered_json b = {{"b", 2}, {"a", 1}};

        ordered_diff_result result = ordered_diff(a, b, /* ignore_order = */ true);

        CHECK_FALSE(result.has_value_changes());
        CHECK_FALSE(result.has_order_changes());
    }

    SECTION("original objects are not modified")
    {
        ordered_json a = {{"a", 1}, {"b", 2}};
        ordered_json b = {{"b", 3}, {"a", 1}};

        // Snapshot the original dumps.
        std::string a_dump_before = a.dump();
        std::string b_dump_before = b.dump();

        ordered_diff_result result = ordered_diff(a, b);

        // Originals must be unchanged after the diff.
        CHECK(a.dump() == a_dump_before);
        CHECK(b.dump() == b_dump_before);
        CHECK(a.dump() == "{\"a\":1,\"b\":2}");
        CHECK(b.dump() == "{\"b\":3,\"a\":1}");

        // And the diff still found both kinds of changes.
        CHECK(result.has_value_changes());
        CHECK(result.has_order_changes());
    }

    SECTION("deeply nested objects report correct paths")
    {
        ordered_json a = {
            {"level1", {
                {"level2", {
                    {"x", 1}, {"y", 2}
                }}
            }}
        };
        ordered_json b = {
            {"level1", {
                {"level2", {
                    {"y", 2}, {"x", 9}
                }}
            }}
        };

        ordered_diff_result result = ordered_diff(a, b);

        CHECK(result.has_value_changes());
        REQUIRE(result.value_changes.size() == 1);
        CHECK(result.value_changes[0] == "$.level1.level2.x: number value changed");

        CHECK(result.has_order_changes());
        REQUIRE(result.order_changes.size() == 1);
        CHECK(result.order_changes[0] ==
              "$.level1.level2: member order changed from [x, y] to [y, x]");
    }

    SECTION("multiple order changes at different paths")
    {
        ordered_json a = {
            {"a", {{"x", 1}, {"y", 2}}},
            {"b", {{"p", 3}, {"q", 4}}}
        };
        ordered_json b = {
            {"a", {{"y", 2}, {"x", 1}}},
            {"b", {{"q", 4}, {"p", 3}}}
        };

        ordered_diff_result result = ordered_diff(a, b);

        CHECK_FALSE(result.has_value_changes());

        CHECK(result.has_order_changes());
        REQUIRE(result.order_changes.size() == 2);
        CHECK(result.order_changes[0] ==
              "$.a: member order changed from [x, y] to [y, x]");
        CHECK(result.order_changes[1] ==
              "$.b: member order changed from [p, q] to [q, p]");
    }
}
