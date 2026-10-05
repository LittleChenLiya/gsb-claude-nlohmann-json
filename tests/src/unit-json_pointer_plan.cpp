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

#include <string>

namespace
{
using plan_type = nlohmann::json_pointer_plan<json>;
using plan_status = plan_type::status;
} // namespace

TEST_CASE("JSON pointer batch plan")
{
    SECTION("shared prefixes are accessed once")
    {
        json j =
        {
            {"a", {{"x", 1}, {"y", 2}, {"n", nullptr}}}
        };

        plan_type plan;
        CHECK(plan.add("/a/x") == 0);
        CHECK(plan.add("/a/y") == 1);
        CHECK(plan.add("/a/n") == 2);
        CHECK(plan.request_count() == 3);
        CHECK(plan.unique_count() == 3);

        plan_type::statistics stats;
        const auto results = plan.execute(j, &stats);

        REQUIRE(results.size() == 3);
        CHECK(results[0].ok());
        CHECK(results[0].outcome == plan_status::value);
        CHECK(results[0].value == 1);
        CHECK(results[1].ok());
        CHECK(results[1].value == 2);

        // an existing null value is a successful result of its own kind
        CHECK(results[2].ok());
        CHECK(results[2].outcome == plan_status::null_value);
        CHECK(results[2].value.is_null());
        CHECK(results[2].error_source == nullptr);

        // the shared prefix "/a" was accessed exactly once for both requests
        CHECK(stats.prefix_visits.at("") == 1);
        CHECK(stats.prefix_visits.at("/a") == 1);
        CHECK(stats.prefix_visits.at("/a/x") == 1);
        CHECK(stats.prefix_visits.at("/a/y") == 1);
        CHECK(stats.prefix_visits.at("/a/n") == 1);
        CHECK(stats.nodes_visited == 5);
    }

    SECTION("a failing prefix fails all dependent requests with the same error source")
    {
        json j = {{"b", 1}};

        plan_type plan;
        plan.add("/a/x");
        plan.add("/a/y");
        plan.add("/a/y/deeper");

        plan_type::statistics stats;
        const auto results = plan.execute(j, &stats);

        REQUIRE(results.size() == 3);
        for (const auto& res : results)
        {
            CHECK(!res.ok());
            CHECK(res.outcome == plan_status::missing_key);
            REQUIRE(res.error_source != nullptr);
            CHECK(res.error_source->kind == plan_status::missing_key);
            CHECK(res.error_source->prefix == "/a");
            CHECK(res.error_source->token == "a");
        }
        // all affected requests share the very same error source
        CHECK(results[0].error_source == results[1].error_source);
        CHECK(results[1].error_source == results[2].error_source);

        // the failing prefix itself was never descended into
        CHECK(stats.prefix_visits.find("/a") == stats.prefix_visits.end());
    }

    SECTION("error classification")
    {
        json j =
        {
            {"object", {{"x", 1}}},
            {"array", {10, 20}},
            {"scalar", 42},
            {"null", nullptr}
        };

        plan_type plan;
        plan.add("/object/y");        // missing object key
        plan.add("/array/2");         // index past the end
        plan.add("/array/-");         // '-' never addresses an element
        plan.add("/array/01");        // leading zero
        plan.add("/array/one");       // not a number
        plan.add("/scalar/x");        // token applied to a number
        plan.add("/null/x");          // token applied to null
        plan.add("/object/x/y");      // token applied below a number

        const auto results = plan.execute(j);

        REQUIRE(results.size() == 8);
        CHECK(results[0].outcome == plan_status::missing_key);
        CHECK(results[0].error_source->prefix == "/object/y");
        CHECK(results[1].outcome == plan_status::array_out_of_bounds);
        CHECK(results[2].outcome == plan_status::array_out_of_bounds);
        CHECK(results[3].outcome == plan_status::array_out_of_bounds);
        CHECK(results[4].outcome == plan_status::array_out_of_bounds);
        CHECK(results[5].outcome == plan_status::wrong_container);
        CHECK(results[6].outcome == plan_status::wrong_container);
        CHECK(results[7].outcome == plan_status::wrong_container);
        CHECK(results[7].error_source->prefix == "/object/x/y");
    }

    SECTION("numeric object keys are not treated as array indices")
    {
        json j =
        {
            {"0", "zero"},
            {"a", {{"0", 42}, {"1", 43}}}
        };

        plan_type plan;
        plan.add("/0");
        plan.add("/a/0");
        plan.add("/a/1");

        const auto results = plan.execute(j);

        REQUIRE(results.size() == 3);
        CHECK(results[0].ok());
        CHECK(results[0].value == "zero");
        CHECK(results[1].ok());
        CHECK(results[1].value == 42);
        CHECK(results[2].ok());
        CHECK(results[2].value == 43);
    }

    SECTION("array indices still work for arrays")
    {
        json j = {{"a", {10, 20, 30}}};

        plan_type plan;
        plan.add("/a/0");
        plan.add("/a/2");

        const auto results = plan.execute(j);

        REQUIRE(results.size() == 2);
        CHECK(results[0].ok());
        CHECK(results[0].value == 10);
        CHECK(results[1].ok());
        CHECK(results[1].value == 30);
    }

    SECTION("the document is not modified and results are independently owned")
    {
        json j = {{"a", {{"x", 1}}}};
        const std::string before = j.dump();

        plan_type plan;
        plan.add("/a");
        plan.add("/a/missing"); // operator[] would insert a null here

        auto results = plan.execute(j);

        // no fields were inserted into the original document
        CHECK(j.dump() == before);

        REQUIRE(results.size() == 2);
        REQUIRE(results[0].ok());

        // changing the original document afterwards does not change results
        j["a"]["x"] = 99;
        j["b"] = "new";
        CHECK(results[0].value == json({{"x", 1}}));

        // and changing a result does not change the document
        results[0].value["x"] = -1;
        CHECK(j["a"]["x"] == 99);
    }

    SECTION("duplicate queries keep their identity")
    {
        json j = {{"a", {{"x", 1}}}};

        plan_type plan;
        const auto first = plan.add("/a/x");
        const auto second = plan.add("/a/x");
        CHECK(first == 0);
        CHECK(second == 1);
        CHECK(plan.request_count() == 2);
        CHECK(plan.unique_count() == 1);

        plan_type::statistics stats;
        const auto results = plan.execute(j, &stats);

        REQUIRE(results.size() == 2);
        CHECK(results[0].ok());
        CHECK(results[1].ok());
        CHECK(results[0].value == 1);
        CHECK(results[1].value == 1);
        // each duplicate keeps its own slot, linked to the first identical query
        CHECK(results[0].canonical == 0);
        CHECK(results[1].canonical == 0);
        // the duplicate was resolved in a single traversal
        CHECK(stats.prefix_visits.at("/a/x") == 1);
        CHECK(stats.nodes_visited == 3);
    }

    SECTION("duplicate failing queries share the error source")
    {
        json j = {{"a", 1}};

        plan_type plan;
        plan.add("/missing");
        plan.add("/missing");

        const auto results = plan.execute(j);

        REQUIRE(results.size() == 2);
        CHECK(!results[0].ok());
        CHECK(!results[1].ok());
        CHECK(results[0].error_source == results[1].error_source);
    }

    SECTION("empty pointer addresses the whole document")
    {
        json j = {{"a", 1}};

        plan_type plan;
        plan.add("");

        const auto results = plan.execute(j);

        REQUIRE(results.size() == 1);
        CHECK(results[0].ok());
        CHECK(results[0].value == j);
    }

    SECTION("escaped reference tokens")
    {
        json j = {{"a/b", {{"~key", 7}}}};

        plan_type plan;
        plan.add("/a~1b/~0key");

        const auto results = plan.execute(j);

        REQUIRE(results.size() == 1);
        CHECK(results[0].ok());
        CHECK(results[0].value == 7);
    }

    SECTION("plain at() is unchanged")
    {
        json j = {{"a", {{"x", 1}}}};

        CHECK(j.at(json::json_pointer("/a/x")) == 1);
        CHECK_THROWS_AS(j.at(json::json_pointer("/a/missing")), json::out_of_range&);
        CHECK(j.contains(json::json_pointer("/a/x")));
    }
}
