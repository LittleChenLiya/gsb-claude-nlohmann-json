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
#ifdef JSON_TEST_NO_GLOBAL_UDLS
    using namespace nlohmann::literals; // NOLINT(google-build-using-namespace)
#endif

namespace
{
// apply the operations one by one and return every intermediate document,
// so tests can verify that each operation's array indices refer to the
// array state produced by the operations before it
std::vector<json> apply_stepwise(const json& source, const json& patch)
{
    std::vector<json> states;
    json doc = source;
    for (const auto& operation : patch)
    {
        doc.patch_inplace(json::array({operation}));
        states.push_back(doc);
    }
    return states;
}

// call json::keyed_diff for its exception without triggering
// JSON_HEDLEY_WARN_UNUSED_RESULT
void discard_keyed_diff(const json& source, const json& target,
                        const std::map<std::string, std::string>& array_id_fields)
{
    const json result = json::keyed_diff(source, target, array_id_fields);
    static_cast<void>(result);
}
} // namespace

TEST_CASE("keyed JSON diff")
{
    SECTION("reordering prefers a single move")
    {
        const json source = json::parse(R"([{"id": "a"}, {"id": "b"}, {"id": "c"}])");
        const json target = json::parse(R"([{"id": "c"}, {"id": "a"}, {"id": "b"}])");

        const json patch = json::keyed_diff(source, target, {{"", "id"}});

        // [a, b, c] -> [c, a, b] takes exactly one move
        CHECK(patch == json::parse(R"([{"op": "move", "from": "/2", "path": "/0"}])"));
        CHECK(source.patch(patch) == target);
    }

    SECTION("content of a moved element is patched at its new position")
    {
        const json source = json::parse(R"([{"id": "a", "v": 1}, {"id": "b", "v": 2}, {"id": "c", "v": 3}])");
        const json target = json::parse(R"([{"id": "c", "v": 42}, {"id": "a", "v": 1}, {"id": "b", "v": 2}])");

        const json patch = json::keyed_diff(source, target, {{"", "id"}});

        // first the move, then the content fix at the moved-to index
        CHECK(patch == json::parse(R"([
            {"op": "move", "from": "/2", "path": "/0"},
            {"op": "replace", "path": "/0/v", "value": 42}
        ])"));
        CHECK(source.patch(patch) == target);
    }

    SECTION("simultaneous additions and removals")
    {
        const json source = json::parse(R"([{"id": "a"}, {"id": "b"}, {"id": "c"}])");
        const json target = json::parse(R"([{"id": "d"}, {"id": "c"}, {"id": "e"}])");

        const json patch = json::keyed_diff(source, target, {{"", "id"}});

        // removals first (highest index first), then the target is walked in
        // order: "d" is added in front, "c" already sits at its index, "e" is
        // appended
        CHECK(patch == json::parse(R"([
            {"op": "remove", "path": "/1"},
            {"op": "remove", "path": "/0"},
            {"op": "add", "path": "/0", "value": {"id": "d"}},
            {"op": "add", "path": "/2", "value": {"id": "e"}}
        ])"));
        CHECK(source.patch(patch) == target);
    }

    SECTION("indices account for previous removals and moves")
    {
        const json source = json::parse(R"([{"id": "x"}, {"id": "a"}, {"id": "b"}, {"id": "c"}])");
        const json target = json::parse(R"([{"id": "c"}, {"id": "a"}, {"id": "b"}])");

        const json patch = json::keyed_diff(source, target, {{"", "id"}});

        CHECK(patch == json::parse(R"([
            {"op": "remove", "path": "/0"},
            {"op": "move", "from": "/2", "path": "/0"}
        ])"));

        // check every intermediate state step by step
        const auto states = apply_stepwise(source, patch);
        REQUIRE(states.size() == 2);
        CHECK(states[0] == json::parse(R"([{"id": "a"}, {"id": "b"}, {"id": "c"}])"));
        CHECK(states[1] == target);
    }

    SECTION("nested arrays")
    {
        const json source = json::parse(
                                R"({"name": "root", "items": [{"id": 1, "tags": ["x", "y"]}, {"id": 2, "tags": ["z"]}]})");
        const json target = json::parse(
                                R"({"name": "root", "items": [{"id": 2, "tags": ["z", "w"]}, {"id": 1, "tags": ["x", "y"]}]})");

        const json patch = json::keyed_diff(source, target, {{"/items", "id"}});

        // the objects are matched by "id"; the unconfigured nested "tags"
        // array keeps positional semantics
        CHECK(patch == json::parse(R"([
            {"op": "move", "from": "/items/1", "path": "/items/0"},
            {"op": "add", "path": "/items/0/tags/-", "value": "w"}
        ])"));
        CHECK(source.patch(patch) == target);
    }

    SECTION("deeply nested configured arrays")
    {
        const json source = json::parse(R"({"a": {"b": {"list": [{"id": "u"}, {"id": "v"}]}}})");
        const json target = json::parse(R"({"a": {"b": {"list": [{"id": "v"}, {"id": "u"}]}}})");

        const json patch = json::keyed_diff(source, target, {{"/a/b/list", "id"}});

        CHECK(patch == json::parse(R"([{"op": "move", "from": "/a/b/list/1", "path": "/a/b/list/0"}])"));
        CHECK(source.patch(patch) == target);
    }

    SECTION("numeric and string IDs are distinct")
    {
        const json source = json::parse(R"([{"id": 1, "v": "x"}])");
        const json target = json::parse(R"([{"id": "1", "v": "x"}])");

        const json patch = json::keyed_diff(source, target, {{"", "id"}});

        // 1 and "1" are different identities: remove + add, no match
        CHECK(patch == json::parse(R"([
            {"op": "remove", "path": "/0"},
            {"op": "add", "path": "/0", "value": {"id": "1", "v": "x"}}
        ])"));
        CHECK(source.patch(patch) == target);
    }

    SECTION("signed and unsigned integers with the same value match")
    {
        const json source = json::array({json::object({{"id", 1}, {"v", "x"}})});
        const json target = json::array({json::object({{"id", 1u}, {"v", "x"}})});

        const json patch = json::keyed_diff(source, target, {{"", "id"}});

        CHECK(patch.empty());
        CHECK(source.patch(patch) == target);
    }

    SECTION("unconfigured arrays keep positional semantics")
    {
        const json source = json::parse(R"([1, 2, 3])");
        const json target = json::parse(R"([3, 2, 1])");

        // no configuration at all
        CHECK(json::keyed_diff(source, target, {}) == json::diff(source, target));
        // configuration for another path does not affect this array
        CHECK(json::keyed_diff(source, target, {{"/other", "id"}}) == json::diff(source, target));
    }

    SECTION("equal values yield an empty patch")
    {
        const json value = json::parse(R"({"items": [{"id": "a"}, {"id": "b"}]})");
        CHECK(json::keyed_diff(value, value, {{"/items", "id"}}).empty());
    }

    SECTION("different types at a configured path replace the value")
    {
        const json source = json::parse(R"([{"id": "a"}])");
        const json target = json::parse(R"({"id": "a"})");

        const json patch = json::keyed_diff(source, target, {{"", "id"}});
        CHECK(patch == json::parse(R"([{"op": "replace", "path": "", "value": {"id": "a"}}])"));
        CHECK(source.patch(patch) == target);
    }

    SECTION("invalid identity data rejects the whole diff")
    {
        const json valid_source = json::parse(R"([{"id": "a"}, {"id": "b"}])");
        const json valid_target = json::parse(R"([{"id": "b"}, {"id": "a"}])");

        SECTION("duplicate ID in source")
        {
            const json source = json::parse(R"([{"id": "a"}, {"id": "a"}, {"id": "c"}])");
            CHECK_THROWS_AS(discard_keyed_diff(source, valid_target, {{"", "id"}}), json::other_error);
            CHECK_THROWS_WITH(discard_keyed_diff(source, valid_target, {{"", "id"}}),
            "[json.exception.other_error.505] keyed_diff: duplicate ID \"a\" in array ''");
        }

        SECTION("duplicate ID in target")
        {
            const json target = json::parse(R"([{"id": "b"}, {"id": "b"}])");
            CHECK_THROWS_AS(discard_keyed_diff(valid_source, target, {{"", "id"}}), json::other_error);
            CHECK_THROWS_WITH(discard_keyed_diff(valid_source, target, {{"", "id"}}),
            "[json.exception.other_error.505] keyed_diff: duplicate ID \"b\" in array ''");
        }

        SECTION("duplicate IDs are rejected even for equal values")
        {
            const json value = json::parse(R"([{"id": "a"}, {"id": "a"}])");
            CHECK_THROWS_AS(discard_keyed_diff(value, value, {{"", "id"}}), json::other_error);
        }

        SECTION("missing ID member")
        {
            const json source = json::parse(R"([{"id": "a"}, {"name": "b"}])");
            CHECK_THROWS_AS(discard_keyed_diff(source, valid_target, {{"", "id"}}), json::other_error);
            CHECK_THROWS_WITH(discard_keyed_diff(source, valid_target, {{"", "id"}}),
            "[json.exception.other_error.503] keyed_diff: element 1 of array '' has no ID member 'id'");
        }

        SECTION("element is not an object")
        {
            const json source = json::parse(R"([{"id": "a"}, 42])");
            CHECK_THROWS_AS(discard_keyed_diff(source, valid_target, {{"", "id"}}), json::other_error);
            CHECK_THROWS_WITH(discard_keyed_diff(source, valid_target, {{"", "id"}}),
            "[json.exception.other_error.503] keyed_diff: element 1 of array '' has no ID member 'id'");
        }

        SECTION("ID of unsupported type")
        {
            const std::vector<json> bad_ids {1.5, true, nullptr, json::array({1}), json::object()};
            for (const auto& bad_id : bad_ids)
            {
                const json source = json::array({json::object({{"id", bad_id}}), json::object({{"id", "a"}})});
                CHECK_THROWS_AS(discard_keyed_diff(source, valid_target, {{"", "id"}}), json::other_error);
            }

            const json source = json::parse(R"([{"id": 1.5}, {"id": "a"}])");
            CHECK_THROWS_WITH(discard_keyed_diff(source, valid_target, {{"", "id"}}),
            "[json.exception.other_error.504] keyed_diff: ID member 'id' of element 0 of array '' must be a string or an integer");
        }

        SECTION("no fallback to positional comparison")
        {
            // the arrays have the same size and would produce a valid
            // positional diff, but the missing ID must reject the diff
            const json source = json::parse(R"([{"id": "a"}, {"name": "b"}])");
            const json target = json::parse(R"([{"id": "a"}, {"id": "b"}])");
            CHECK_THROWS_AS(discard_keyed_diff(source, target, {{"", "id"}}), json::other_error);
        }
    }

    SECTION("roundtrip property for assorted inputs")
    {
        const std::vector<std::pair<std::string, std::string>> cases
        {
            {R"([{"id": "a"}, {"id": "b"}, {"id": "c"}])", R"([{"id": "c"}, {"id": "b"}, {"id": "a"}])"},
            {R"([{"id": "a"}, {"id": "b"}, {"id": "c"}])", R"([{"id": "b"}, {"id": "c"}, {"id": "a"}])"},
            {R"([{"id": "a"}, {"id": "b"}])", R"([{"id": "c"}, {"id": "d"}, {"id": "e"}])"},
            {R"([{"id": "a", "x": 1}, {"id": "b"}])", R"([{"id": "b", "y": 2}, {"id": "a", "x": 2}, {"id": "c"}])"},
            {R"([])", R"([{"id": "a"}, {"id": "b"}])"},
            {R"([{"id": "a"}, {"id": "b"}])", R"([])"},
            {R"([{"id": 1}, {"id": 2}, {"id": 3}])", R"([{"id": 3}, {"id": 1}])"}
        };

        for (const auto& test_case : cases)
        {
            const json source = json::parse(test_case.first);
            const json target = json::parse(test_case.second);
            const json patch = json::keyed_diff(source, target, {{"", "id"}});

            CAPTURE(patch);
            CHECK(source.patch(patch) == target);
            // every intermediate step must be a valid document, too
            const auto states = apply_stepwise(source, patch);
            if (!states.empty())
            {
                CHECK(states.back() == target);
            }
        }
    }
}
