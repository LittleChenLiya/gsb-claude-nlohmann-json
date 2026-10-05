//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/json.hpp>
#include <nlohmann/ordered_diff.hpp>

using nlohmann::json;
using nlohmann::ordered_json;
namespace odiff = nlohmann::ordered_diff;

namespace
{
std::size_t count_kind(const odiff::result& res, odiff::change_kind kind)
{
    std::size_t n = 0;
    for (const auto& c : res.changes)
    {
        if (c.kind == kind)
        {
            ++n;
        }
    }
    return n;
}
} // namespace

TEST_CASE("ordered_json explicit diff")
{
    SECTION("same members, different order: only an order change")
    {
        const ordered_json old_doc = ordered_json::parse(R"({"a":1,"b":2})");
        const ordered_json new_doc = ordered_json::parse(R"({"b":2,"a":1})");

        const odiff::result res = odiff::diff(old_doc, new_doc);

        REQUIRE(res.changes.size() == 1);
        CHECK(res.has_order_changes());
        CHECK_FALSE(res.has_value_changes());

        const auto& c = res.changes.front();
        CHECK(c.kind == odiff::change_kind::order_changed);
        CHECK(c.path.empty()); // root object
        // order is reported by member name, not by iterator
        CHECK(c.old_members == std::vector<std::string>{"a", "b"});
        CHECK(c.new_members == std::vector<std::string>{"b", "a"});
    }

    SECTION("different order and changed value: both kinds appear")
    {
        const ordered_json old_doc = ordered_json::parse(R"({"a":1,"b":2})");
        const ordered_json new_doc = ordered_json::parse(R"({"b":3,"a":1})");

        const odiff::result res = odiff::diff(old_doc, new_doc);

        CHECK(res.has_order_changes());
        CHECK(res.has_value_changes());
        CHECK(count_kind(res, odiff::change_kind::order_changed) == 1);
        REQUIRE(count_kind(res, odiff::change_kind::value_changed) == 1);

        const auto* value_change = &res.changes[0];
        const auto* order_change = &res.changes[0];
        for (const auto& c : res.changes)
        {
            if (c.kind == odiff::change_kind::value_changed)
            {
                value_change = &c;
            }
            if (c.kind == odiff::change_kind::order_changed)
            {
                order_change = &c;
            }
        }
        CHECK(value_change->path == "/b");
        CHECK(value_change->old_value == 2);
        CHECK(value_change->new_value == 3);
        CHECK(order_change->old_members == std::vector<std::string>{"a", "b"});
        CHECK(order_change->new_members == std::vector<std::string>{"b", "a"});
    }

    SECTION("nested objects are compared by their actual order")
    {
        const ordered_json old_doc = ordered_json::parse(R"({"outer":{"x":1,"y":2},"z":0})");
        const ordered_json new_doc = ordered_json::parse(R"({"outer":{"y":2,"x":1},"z":0})");

        const odiff::result res = odiff::diff(old_doc, new_doc);

        REQUIRE(res.changes.size() == 1);
        CHECK(res.has_order_changes());
        CHECK_FALSE(res.has_value_changes());
        // the order change is located at the nested object
        CHECK(res.changes.front().path == "/outer");
        CHECK(res.changes.front().old_members == std::vector<std::string>{"x", "y"});
        CHECK(res.changes.front().new_members == std::vector<std::string>{"y", "x"});
    }

    SECTION("nested value change is located at the nested member")
    {
        const ordered_json old_doc = ordered_json::parse(R"({"outer":{"x":1,"y":2}})");
        const ordered_json new_doc = ordered_json::parse(R"({"outer":{"x":1,"y":3}})");

        const odiff::result res = odiff::diff(old_doc, new_doc);

        REQUIRE(res.changes.size() == 1);
        CHECK(res.has_value_changes());
        CHECK_FALSE(res.has_order_changes());
        CHECK(res.changes.front().kind == odiff::change_kind::value_changed);
        CHECK(res.changes.front().path == "/outer/y");
        CHECK(res.changes.front().old_value == 2);
        CHECK(res.changes.front().new_value == 3);
    }

    SECTION("ignore_object_order hides order changes but not value changes")
    {
        odiff::options opt;
        opt.ignore_object_order = true;

        // pure reorder: nothing is reported anymore
        const ordered_json old_doc = ordered_json::parse(R"({"a":1,"b":2})");
        const ordered_json new_doc = ordered_json::parse(R"({"b":2,"a":1})");
        CHECK(odiff::diff(old_doc, new_doc, opt).empty());

        // reorder plus value change: the value change is still reported
        const ordered_json new_doc2 = ordered_json::parse(R"({"b":3,"a":1})");
        const odiff::result res = odiff::diff(old_doc, new_doc2, opt);
        CHECK_FALSE(res.has_order_changes());
        REQUIRE(res.changes.size() == 1);
        CHECK(res.changes.front().kind == odiff::change_kind::value_changed);
        CHECK(res.changes.front().path == "/b");
    }

    SECTION("arrays keep positional semantics")
    {
        const ordered_json old_doc = ordered_json::parse(R"({"list":[1,2,3]})");
        const ordered_json new_doc = ordered_json::parse(R"({"list":[1,4,3,5]})");

        const odiff::result res = odiff::diff(old_doc, new_doc);

        REQUIRE(res.changes.size() == 2);
        CHECK(res.changes[0].kind == odiff::change_kind::value_changed);
        CHECK(res.changes[0].path == "/list/1");
        CHECK(res.changes[0].old_value == 2);
        CHECK(res.changes[0].new_value == 4);
        CHECK(res.changes[1].kind == odiff::change_kind::added);
        CHECK(res.changes[1].path == "/list/3");
        CHECK(res.changes[1].new_value == 5);
    }

    SECTION("numbers are compared along the library's native types")
    {
        // same number, different JSON number type: equal per library semantics
        CHECK(odiff::diff(ordered_json(1), ordered_json(1.0)).empty());
        CHECK(odiff::diff(ordered_json(1), ordered_json(1u)).empty());

        // actually different numbers: a value change
        const odiff::result res = odiff::diff(ordered_json(1), ordered_json(2));
        REQUIRE(res.changes.size() == 1);
        CHECK(res.changes.front().kind == odiff::change_kind::value_changed);
        CHECK(res.changes.front().old_value == 1);
        CHECK(res.changes.front().new_value == 2);
    }

    SECTION("added and removed members are value changes, not order changes")
    {
        const ordered_json old_doc = ordered_json::parse(R"({"a":1,"b":2})");
        const ordered_json new_doc = ordered_json::parse(R"({"a":1,"c":3})");

        const odiff::result res = odiff::diff(old_doc, new_doc);

        CHECK(res.has_value_changes());
        CHECK_FALSE(res.has_order_changes());
        REQUIRE(res.changes.size() == 2);
        CHECK(res.changes[0].kind == odiff::change_kind::removed);
        CHECK(res.changes[0].path == "/b");
        CHECK(res.changes[0].old_value == 2);
        CHECK(res.changes[1].kind == odiff::change_kind::added);
        CHECK(res.changes[1].path == "/c");
        CHECK(res.changes[1].new_value == 3);
    }

    SECTION("identical documents produce no changes")
    {
        const ordered_json doc = ordered_json::parse(R"({"a":1,"b":[1,2,{"c":"x"}]})");
        CHECK(odiff::diff(doc, doc).empty());
    }

    SECTION("input documents are left unchanged")
    {
        ordered_json old_doc = ordered_json::parse(R"({"a":1,"b":{"x":1,"y":2}})");
        ordered_json new_doc = ordered_json::parse(R"({"b":{"y":2,"x":9},"a":1})");
        const ordered_json old_copy = old_doc;
        const ordered_json new_copy = new_doc;

        const odiff::result res = odiff::diff(old_doc, new_doc);
        CHECK(res.has_value_changes());
        CHECK(res.has_order_changes());

        CHECK(old_doc == old_copy);
        CHECK(new_doc == new_copy);
    }

    SECTION("rendered output mentions member names for order changes")
    {
        const ordered_json old_doc = ordered_json::parse(R"({"a":1,"b":2})");
        const ordered_json new_doc = ordered_json::parse(R"({"b":3,"a":1})");

        const std::string out = odiff::diff(old_doc, new_doc).str();
        CHECK(out.find("order changed at : [a, b] -> [b, a]") != std::string::npos);
        CHECK(out.find("value changed at /b: 2 -> 3") != std::string::npos);
    }

    SECTION("plain json keeps its unordered object semantics")
    {
        // operator== stays order-insensitive for nlohmann::json
        const json j1 = json::parse(R"({"a":1,"b":2})");
        const json j2 = json::parse(R"({"b":2,"a":1})");
        CHECK(j1 == j2);

        // json::diff (RFC 6902) still sees no difference
        CHECK(json::diff(j1, j2).empty());
    }
}
