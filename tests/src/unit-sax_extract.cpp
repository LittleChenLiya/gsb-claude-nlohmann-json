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

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
using extracted = std::vector<std::pair<std::string, json>>;

// run sax_extract and collect all delivered subtrees
nlohmann::sax_extract_result<> collect(const std::string& input,
                                       const std::vector<std::string>& pointers,
                                       extracted& out,
                                       const nlohmann::sax_extract_limits& limits = {})
{
    return nlohmann::sax_extract(input, pointers, [&out](const std::string & p, json v)
    {
        out.emplace_back(p, std::move(v));
        return true;
    }, limits);
}
} // namespace

TEST_CASE("SAX multi-pointer extraction")
{
    SECTION("acceptance: parent/child pointers and escaped tokens")
    {
        const std::string doc = R"({"a":[{"id":1},{"id":2}],"x/y":{"~k":3}})";
        extracted out;
        const auto res = collect(doc, {"/a", "/a/1/id", "/x~1y/~0k"}, out);

        REQUIRE(res.complete);
        CHECK(res.emitted == 3);
        CHECK(res.unmatched.empty());

        // callbacks arrive in subtree completion order: the nested /a/1/id
        // completes before its parent /a
        REQUIRE(out.size() == 3);
        CHECK(out[0].first == "/a/1/id");
        CHECK(out[1].first == "/a");
        CHECK(out[2].first == "/x~1y/~0k");

        // parent and child are both delivered, and the escaped path yields 3
        CHECK(out[0].second == 2);
        CHECK(out[1].second == json::parse(R"([{"id":1},{"id":2}])"));
        CHECK(out[2].second == 3);

        // cross-check every extraction against DOM indexing
        const json dom = json::parse(doc);
        for (const auto& hit : out)
        {
            CHECK(hit.second == dom.at(json::json_pointer(hit.first)));
        }
    }

    SECTION("empty pointer extracts the whole document")
    {
        extracted out;
        const auto res = collect(R"({"a":1,"b":[true,null]})", {""}, out);
        REQUIRE(res.complete);
        REQUIRE(out.size() == 1);
        CHECK(out[0].first.empty());
        CHECK(out[0].second == json::parse(R"({"a":1,"b":[true,null]})"));
    }

    SECTION("empty object keys and array indices")
    {
        const std::string doc = R"({"":{"x":1},"list":[10,20,30]})";
        extracted out;
        const auto res = collect(doc, {"/", "//x", "/list/0", "/list/2"}, out);
        REQUIRE(res.complete);
        REQUIRE(out.size() == 4);
        const json dom = json::parse(doc);
        for (const auto& hit : out)
        {
            CHECK(hit.second == dom.at(json::json_pointer(hit.first)));
        }
        CHECK(out[0].second == 1);     // //x completes first (innermost)
        CHECK(out[1].second == json({{"x", 1}})); // "/" is the empty key
        CHECK(out[2].second == 10);
        CHECK(out[3].second == 30);
    }

    SECTION("duplicate requests are delivered only once")
    {
        extracted out;
        const auto res = collect(R"({"a":1})", {"/a", "/a", "/a"}, out);
        REQUIRE(res.complete);
        CHECK(res.emitted == 1);
        REQUIRE(out.size() == 1);
        CHECK(out[0].second == 1);
    }

    SECTION("missing pointers are reported together at EOF")
    {
        extracted out;
        const auto res = collect(R"({"a":[1,2]})", {"/missing", "/a/0", "/a/5", "/other/x"}, out);
        REQUIRE(res.complete);
        CHECK(res.emitted == 1);
        REQUIRE(out.size() == 1);
        CHECK(out[0].second == 1);
        // reported in request order once the input is exhausted
        CHECK(res.unmatched == std::vector<std::string>({"/missing", "/a/5", "/other/x"}));
    }

    SECTION("syntax of unselected data is still fully validated")
    {
        // large unselected object followed by an invalid tail
        std::string doc = R"({"hit":42,"pad":[)";
        for (int i = 0; i < 1000; ++i)
        {
            doc += (i != 0) ? ",{\"k\":[]}" : "{\"k\":[]}";
        }
        doc += "]},"; // invalid trailing garbage after the document

        extracted out;
        const auto res = collect(doc, {"/hit"}, out);
        CHECK_FALSE(res.complete);
        CHECK(res.parse_failed);
        CHECK_FALSE(res.error.empty());
        // the match completed before the invalid tail was reached
        CHECK(res.emitted == 1);
        REQUIRE(out.size() == 1);
        CHECK(out[0].second == 42);
    }

    SECTION("duplicate keys making a requested path ambiguous are rejected")
    {
        const std::string doc = R"({"a":[{"id":1},{"id":2,"id":3}]})";
        extracted out;
        CHECK_THROWS_WITH_AS(collect(doc, {"/a/1/id"}, out),
                             "[json.exception.parse_error.112] parse error: duplicate object member 'id' at JSON pointer '/a/1' makes a requested extraction path ambiguous",
                             json::parse_error&);
    }

    SECTION("duplicate keys on unrequested paths keep the original behavior")
    {
        const std::string doc = R"({"a":{"id":1,"id":2},"b":3})";
        extracted out;
        const auto res = collect(doc, {"/b"}, out);
        REQUIRE(res.complete);
        REQUIRE(out.size() == 1);
        CHECK(out[0].second == 3);
        // DOM parsing agrees on the unrequested duplicate (last one wins)
        CHECK(json::parse(doc)["a"]["id"] == 2);
    }

    SECTION("subtree byte budget")
    {
        const std::string doc = R"({"a":{"bbbb":"cccccccccc","d":[1,2,3]},"z":0})";
        extracted out;
        nlohmann::sax_extract_limits limits;
        limits.max_subtree_bytes = 8;
        const auto res = collect(doc, {"/a"}, out, limits);
        CHECK_FALSE(res.complete);
        CHECK(res.limit_exceeded);
        CHECK(res.emitted == 0);
        CHECK(out.empty());
        CHECK(res.unmatched == std::vector<std::string>({"/a"}));
    }

    SECTION("subtree depth budget")
    {
        const std::string doc = R"({"a":{"b":{"c":1}},"z":0})";
        extracted out;
        nlohmann::sax_extract_limits limits;
        limits.max_subtree_depth = 1;
        const auto res = collect(doc, {"/a"}, out, limits);
        CHECK_FALSE(res.complete);
        CHECK(res.limit_exceeded);
        CHECK(res.emitted == 0);
        CHECK(out.empty());
    }

    SECTION("generous limits let the same document through")
    {
        const std::string doc = R"({"a":{"b":{"c":1}},"z":0})";
        extracted out;
        nlohmann::sax_extract_limits limits;
        limits.max_subtree_depth = 3;
        limits.max_subtree_bytes = 1024;
        const auto res = collect(doc, {"/a"}, out, limits);
        REQUIRE(res.complete);
        REQUIRE(out.size() == 1);
        CHECK(out[0].second == json::parse(R"({"b":{"c":1}})"));
    }

    SECTION("callback can stop the extraction")
    {
        std::size_t calls = 0;
        const auto res = nlohmann::sax_extract(std::string(R"({"a":[1,2,3]})"),
                                               std::vector<std::string>({"/a/0", "/a/1", "/a/2"}),
                                               [&calls](const std::string&, json)
        {
            return ++calls < 1; // stop after the first delivery
        });
        CHECK_FALSE(res.complete);
        CHECK(res.stopped_by_callback);
        CHECK(res.emitted == 1);
        CHECK(res.unmatched == std::vector<std::string>({"/a/1", "/a/2"}));
    }

    SECTION("wide character input")
    {
        const std::wstring doc = L"{\"a\":[{\"id\":1}],\"x/y\":{\"~k\":3}}";
        extracted out;
        const auto res = nlohmann::sax_extract(doc,
                                               std::vector<std::string>({"/a/0/id", "/x~1y/~0k"}),
                                               [&out](const std::string & p, json v)
        {
            out.emplace_back(p, std::move(v));
            return true;
        });
        REQUIRE(res.complete);
        REQUIRE(out.size() == 2);
        CHECK(out[0].second == 1);
        CHECK(out[1].second == 3);
    }

    SECTION("stream input")
    {
        std::istringstream stream(R"({"a":{"b":[1,2,{"c":null}]},"skip":{"x":[1,2,3]}})");
        extracted out;
        const auto res = nlohmann::sax_extract(stream, std::vector<std::string>({"/a/b/2"}),
                                               [&out](const std::string & p, json v)
        {
            out.emplace_back(p, std::move(v));
            return true;
        });
        REQUIRE(res.complete);
        REQUIRE(out.size() == 1);
        CHECK(out[0].second == json({{"c", nullptr}}));
    }

    SECTION("root scalar document")
    {
        extracted out;
        const auto res = collect("42", {""}, out);
        REQUIRE(res.complete);
        REQUIRE(out.size() == 1);
        CHECK(out[0].second == 42);
    }

    SECTION("invalid pointer syntax is rejected like json_pointer")
    {
        extracted out;
        CHECK_THROWS_AS(collect("{}", {"foo"}, out), json::parse_error&);
        CHECK_THROWS_AS(collect("{}", {"/~2"}, out), json::parse_error&);
    }

    SECTION("DOM cross-check on a mixed document")
    {
        const std::string doc = R"({
            "deep": {"l1": {"l2": {"l3": [0, 1, {"leaf": "value"}]}}},
            "arr": [[1, 2], [3, 4]],
            "num": 3.25,
            "flag": false,
            "nothing": null
        })";
        const std::vector<std::string> pointers =
        {
            "/deep/l1/l2/l3",
            "/deep/l1/l2/l3/2/leaf",
            "/arr",
            "/arr/1",
            "/arr/1/0",
            "/num",
            "/flag",
            "/nothing"
        };
        extracted out;
        const auto res = collect(doc, pointers, out);
        REQUIRE(res.complete);
        CHECK(res.emitted == pointers.size());
        const json dom = json::parse(doc);
        REQUIRE(out.size() == pointers.size());
        for (const auto& hit : out)
        {
            CHECK(hit.second == dom.at(json::json_pointer(hit.first)));
        }
        // nested matches complete before their enclosing matches
        CHECK(out[0].first == "/deep/l1/l2/l3/2/leaf");
        CHECK(out[1].first == "/deep/l1/l2/l3");
        CHECK(out[2].first == "/arr/1/0");
        CHECK(out[3].first == "/arr/1");
        CHECK(out[4].first == "/arr");
    }

    SECTION("non-ASCII strings survive extraction")
    {
        const std::string doc = "{\"unicode\": \"\u00e9\u6c49\u5b57\"}";
        extracted out;
        const auto res = collect(doc, {"/unicode"}, out);
        REQUIRE(res.complete);
        REQUIRE(out.size() == 1);
        CHECK(out[0].second == json::parse(doc)["unicode"]);
    }
}
