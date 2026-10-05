//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/json.hpp>
using nlohmann::json;
using nlohmann::json_pointer_plan;
using nlohmann::json_pointer_error_t;

#include <map>
#include <string>
#include <vector>

namespace
{
// a custom object type that counts find() calls
template<typename Key, typename T, typename IgnoredLess, typename Allocator>
struct counting_map : std::map<Key, T, IgnoredLess, Allocator>
{
    using base = std::map<Key, T, IgnoredLess, Allocator>;
    using base::base;

    mutable int find_count = 0;
    mutable std::map<Key, int> key_counts;

    auto find(const Key& key) -> decltype(base::find(key))
    {
        ++find_count;
        ++key_counts[key];
        return base::find(key);
    }

    auto find(const Key& key) const -> decltype(base::find(key))
    {
        ++find_count;
        ++key_counts[key];
        return base::find(key);
    }
};
} // namespace

TEST_CASE("json_pointer_plan")
{
    SECTION("batch access merges common prefixes")
    {
        // requests /a/x and /a/y share the prefix /a; /a is accessed once
        json doc = {{"a", {{"x", 1}, {"y", 2}}}};

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            json::json_pointer("/a/x"),
            json::json_pointer("/a/y")
        });

        CHECK(plan.request_count() == 2);
        CHECK_FALSE(plan.empty());

        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 2);

        CHECK(results[0].ok());
        CHECK(results[1].ok());
        CHECK(static_cast<bool>(results[0]));
        CHECK(static_cast<bool>(results[1]));
        CHECK(results[0].value == 1);
        CHECK(results[1].value == 2);
        CHECK(results[0].error == json_pointer_error_t::success);
        CHECK(results[1].error == json_pointer_error_t::success);
        CHECK(results[0].failed_prefix.empty());
        CHECK(results[1].failed_prefix.empty());
    }

    SECTION("missing key associates all affected requests with the failed prefix")
    {
        // /a is missing, so both /a/x and /a/y fail with the same source error
        json doc = {{"b", 1}};

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            json::json_pointer("/a/x"),
            json::json_pointer("/a/y")
        });

        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 2);

        CHECK_FALSE(results[0].ok());
        CHECK_FALSE(results[1].ok());
        CHECK(results[0].error == json_pointer_error_t::missing_key);
        CHECK(results[1].error == json_pointer_error_t::missing_key);
        CHECK(results[0].failed_prefix == json::json_pointer("/a"));
        CHECK(results[1].failed_prefix == json::json_pointer("/a"));
    }

    SECTION("null terminal succeeds")
    {
        // a request that points to an existing null value succeeds
        json doc = {{"z", nullptr}};

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            json::json_pointer("/z")
        });

        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 1);

        CHECK(results[0].ok());
        CHECK(results[0].value.is_null());
        CHECK(results[0].error == json_pointer_error_t::success);
    }

    SECTION("duplicate queries preserve identity")
    {
        // the same pointer requested twice yields two independent owned results
        json doc = {{"a", {{"x", 42}}}};

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            json::json_pointer("/a/x"),
            json::json_pointer("/a/x")
        });

        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 2);

        CHECK(results[0].ok());
        CHECK(results[1].ok());
        CHECK(results[0].value == 42);
        CHECK(results[1].value == 42);

        // the results are independent owned copies
        doc["a"]["x"] = 999;
        CHECK(results[0].value == 42);
        CHECK(results[1].value == 42);
    }

    SECTION("numeric object keys are not mistaken for arrays")
    {
        // an object with numeric keys is accessed by key, not by array index
        json doc = {{"a", {{"0", 7}, {"1", 8}}}};

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            json::json_pointer("/a/0"),
            json::json_pointer("/a/1")
        });

        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 2);

        CHECK(results[0].ok());
        CHECK(results[1].ok());
        CHECK(results[0].value == 7);
        CHECK(results[1].value == 8);
    }

    SECTION("execution does not modify the original JSON")
    {
        json doc = {{"a", {{"x", 1}}}};
        std::string const before = doc.dump();

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            json::json_pointer("/a/x"),
            json::json_pointer("/missing/y")
        });
        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);

        std::string const after = doc.dump();
        CHECK(before == after);
        CHECK_FALSE(doc.contains("missing"));
        REQUIRE(results.size() == 2);
        CHECK(results[0].ok());
        CHECK_FALSE(results[1].ok());
    }

    SECTION("results remain independent after the original changes")
    {
        json doc = {{"a", {{"x", {{"deep", 1}}}}}};

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            json::json_pointer("/a/x")
        });
        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 1);
        CHECK(results[0].ok());

        // mutate the original after execution
        doc["a"]["x"]["deep"] = 100;
        doc["a"]["x"]["new"] = 200;

        // the owned result is unaffected
        CHECK(results[0].value["deep"] == 1);
        CHECK_FALSE(results[0].value.contains("new"));
    }

    SECTION("error classification")
    {
        SECTION("wrong container")
        {
            json doc = {{"a", 5}};
            json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({json::json_pointer("/a/x")});
            std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
            REQUIRE(results.size() == 1);
            CHECK_FALSE(results[0].ok());
            CHECK(results[0].error == json_pointer_error_t::wrong_container);
            CHECK(results[0].failed_prefix == json::json_pointer("/a"));
        }

        SECTION("array out of range")
        {
            json doc = {{"a", json::array({1, 2, 3})}};
            json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({json::json_pointer("/a/5")});
            std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
            REQUIRE(results.size() == 1);
            CHECK_FALSE(results[0].ok());
            CHECK(results[0].error == json_pointer_error_t::array_out_of_range);
            CHECK(results[0].failed_prefix == json::json_pointer("/a"));
        }

        SECTION("array index dash")
        {
            json doc = {{"a", json::array({1, 2, 3})}};
            json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({json::json_pointer("/a/-")});
            std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
            REQUIRE(results.size() == 1);
            CHECK_FALSE(results[0].ok());
            CHECK(results[0].error == json_pointer_error_t::array_out_of_range);
            CHECK(results[0].failed_prefix == json::json_pointer("/a"));
        }

        SECTION("null container")
        {
            json doc = {{"a", nullptr}};
            json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({json::json_pointer("/a/x")});
            std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
            REQUIRE(results.size() == 1);
            CHECK_FALSE(results[0].ok());
            CHECK(results[0].error == json_pointer_error_t::null_container);
            CHECK(results[0].failed_prefix == json::json_pointer("/a"));
        }

        SECTION("invalid index")
        {
            json doc = {{"a", json::array({1, 2, 3})}};
            json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({json::json_pointer("/a/01")});
            std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
            REQUIRE(results.size() == 1);
            CHECK_FALSE(results[0].ok());
            CHECK(results[0].error == json_pointer_error_t::invalid_index);
            CHECK(results[0].failed_prefix == json::json_pointer("/a"));
        }
    }

    SECTION("root pointer")
    {
        json doc = {{"a", 1}};
        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({json::json_pointer("")});
        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 1);
        CHECK(results[0].ok());
        CHECK(results[0].value == doc);
    }

    SECTION("empty plan")
    {
        json_pointer_plan<std::string> plan;
        CHECK(plan.empty());
        CHECK(plan.request_count() == 0);
        json doc = {{"a", 1}};
        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        CHECK(results.empty());
    }

    SECTION("compile from iterator range")
    {
        std::vector<json::json_pointer> pointers = {
            json::json_pointer("/a/x"),
            json::json_pointer("/a/y")
        };
        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile(pointers.begin(), pointers.end());
        CHECK(plan.request_count() == 2);
        json doc = {{"a", {{"x", 1}, {"y", 2}}}};
        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 2);
        CHECK(results[0].value == 1);
        CHECK(results[1].value == 2);
    }

    SECTION("mixed success and failure in one plan")
    {
        json doc = {
            {"a", {{"x", 1}}},
            {"b", nullptr},
            {"c", json::array({10, 20})}
        };

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            json::json_pointer("/a/x"),   // success
            json::json_pointer("/a/y"),   // missing key (y)
            json::json_pointer("/b"),     // null terminal, success
            json::json_pointer("/c/0"),   // success
            json::json_pointer("/c/9"),   // array out of range
            json::json_pointer("/d/x")    // missing key (d)
        });

        std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);
        REQUIRE(results.size() == 6);

        CHECK(results[0].ok());
        CHECK(results[0].value == 1);

        CHECK_FALSE(results[1].ok());
        CHECK(results[1].error == json_pointer_error_t::missing_key);
        CHECK(results[1].failed_prefix == json::json_pointer("/a/y"));

        CHECK(results[2].ok());
        CHECK(results[2].value.is_null());

        CHECK(results[3].ok());
        CHECK(results[3].value == 10);

        CHECK_FALSE(results[4].ok());
        CHECK(results[4].error == json_pointer_error_t::array_out_of_range);
        CHECK(results[4].failed_prefix == json::json_pointer("/c"));

        CHECK_FALSE(results[5].ok());
        CHECK(results[5].error == json_pointer_error_t::missing_key);
        CHECK(results[5].failed_prefix == json::json_pointer("/d"));
    }

    SECTION("access each prefix only once")
    {
        using cjson = nlohmann::basic_json<counting_map>;
        using cmap = counting_map<std::string, cjson, std::less<std::string>,
              std::allocator<std::pair<const std::string, cjson>>>;

        cjson doc;
        doc["a"]["x"] = 1;
        doc["a"]["y"] = 2;

        auto* root_obj = doc.get_ptr<cmap*>();
        auto* a_obj = doc["a"].get_ptr<cmap*>();

        json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
            cjson::json_pointer("/a/x"),
            cjson::json_pointer("/a/y")
        });
        std::vector<json_pointer_plan<std::string>::result<cjson>> results = plan.execute(doc);

        REQUIRE(results.size() == 2);
        CHECK(results[0].ok());
        CHECK(results[1].ok());

        // each prefix is accessed exactly once
        CHECK(root_obj->key_counts["a"] == 1);
        CHECK(a_obj->key_counts["x"] == 1);
        CHECK(a_obj->key_counts["y"] == 1);
    }
}
