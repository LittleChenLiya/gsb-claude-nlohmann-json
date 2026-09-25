//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <map>
#include <nlohmann/json.hpp>
using nlohmann::json;
#ifdef JSON_TEST_NO_GLOBAL_UDLS
    using namespace nlohmann::literals; // NOLINT(google-build-using-namespace)
#endif

namespace
{
// id configuration matching the root array on the member "id"
const std::map<json::string_t, json::string_t> root_ids {{"", "id"}};

void check_roundtrip(const json& source, const json& target,
                     const std::map<json::string_t, json::string_t>& array_id_keys)
{
    const json patch = json::keyed_diff(source, target, array_id_keys);
    INFO("patch: ", patch.dump());
    CHECK(source.patch(patch) == target);
}
} // namespace

TEST_CASE("keyed_diff")
{
    SECTION("reorder only: single move")
    {
        // [a, b, c] -> [c, a, b] needs exactly one move
        const json source = R"([{"id": "a"}, {"id": "b"}, {"id": "c"}])"_json;
        const json target = R"([{"id": "c"}, {"id": "a"}, {"id": "b"}])"_json;

        const json patch = json::keyed_diff(source, target, root_ids);
        CHECK(patch == json::array(
        {
            {{"op", "move"}, {"from", "/2"}, {"path", "/0"}}
        }));
        CHECK(source.patch(patch) == target);
    }

    SECTION("move followed by content change of the moved element")
    {
        const json source = R"([{"id": "a", "v": 1}, {"id": "b", "v": 2}, {"id": "c", "v": 3}])"_json;
        const json target = R"([{"id": "c", "v": 30}, {"id": "a", "v": 1}, {"id": "b", "v": 2}])"_json;

        const json patch = json::keyed_diff(source, target, root_ids);
        // the content patch addresses the moved element at its target index
        CHECK(patch == json::array(
        {
            {{"op", "move"}, {"from", "/2"}, {"path", "/0"}},
            {{"op", "replace"}, {"path", "/0/v"}, {"value", 30}}
        }));
        CHECK(source.patch(patch) == target);
    }

    SECTION("simultaneous add and remove: indices account for previous operations")
    {
        const json source = R"([{"id": "a"}, {"id": "b"}, {"id": "c"}])"_json;
        const json target = R"([{"id": "c"}, {"id": "x"}, {"id": "a"}])"_json;

        const json patch = json::keyed_diff(source, target, root_ids);
        CHECK(patch == json::array(
        {
            // remove b first; afterwards the array is [a, c]
            {{"op", "remove"}, {"path", "/1"}},
            // c now sits at index 1 and is moved to the front
            {{"op", "move"}, {"from", "/1"}, {"path", "/0"}},
            // x is added at its target position
            {{"op", "add"}, {"path", "/1"}, {"value", {{"id", "x"}}}}
        }));
        CHECK(source.patch(patch) == target);
    }

    SECTION("removals are emitted highest index first")
    {
        const json source = R"([{"id": "a"}, {"id": "b"}, {"id": "c"}])"_json;
        const json target = R"([{"id": "b"}])"_json;

        const json patch = json::keyed_diff(source, target, root_ids);
        CHECK(patch == json::array(
        {
            {{"op", "remove"}, {"path", "/2"}},
            {{"op", "remove"}, {"path", "/0"}}
        }));
        CHECK(source.patch(patch) == target);
    }

    SECTION("nested arrays")
    {
        SECTION("keyed array nested in an object")
        {
            const json source = R"({"items": [{"id": 1, "tags": ["x", "y"]}, {"id": 2, "tags": ["z"]}]})"_json;
            const json target = R"({"items": [{"id": 2, "tags": ["z", "w"]}, {"id": 1, "tags": ["x", "y"]}]})"_json;

            const std::map<json::string_t, json::string_t> keys {{"/items", "id"}};
            const json patch = json::keyed_diff(source, target, keys);
            CHECK(patch == json::array(
            {
                {{"op", "move"}, {"from", "/items/1"}, {"path", "/items/0"}},
                // unconfigured nested array keeps positional semantics
                {{"op", "add"}, {"path", "/items/0/tags/-"}, {"value", "w"}}
            }));
            CHECK(source.patch(patch) == target);
        }

        SECTION("keyed array nested in a keyed array element")
        {
            const json source = R"([{"id": "p", "subs": [{"id": "s1"}, {"id": "s2"}]}])"_json;
            const json target = R"([{"id": "p", "subs": [{"id": "s2"}, {"id": "s1"}]}])"_json;

            // nested paths are expressed in target indices
            const std::map<json::string_t, json::string_t> keys {{"", "id"}, {"/0/subs", "id"}};
            const json patch = json::keyed_diff(source, target, keys);
            CHECK(patch == json::array(
            {
                {{"op", "move"}, {"from", "/0/subs/1"}, {"path", "/0/subs/0"}}
            }));
            CHECK(source.patch(patch) == target);
        }
    }

    SECTION("numeric and string ids are distinct")
    {
        SECTION("number 1 and string \"1\" do not match")
        {
            const json source = R"([{"id": 1, "v": "num"}])"_json;
            const json target = R"([{"id": "1", "v": "str"}])"_json;

            const json patch = json::keyed_diff(source, target, root_ids);
            CHECK(patch == json::array(
            {
                {{"op", "remove"}, {"path", "/0"}},
                {{"op", "add"}, {"path", "/0"}, {"value", {{"id", "1"}, {"v", "str"}}}}
            }));
            CHECK(source.patch(patch) == target);
        }

        SECTION("mixed numeric and string ids can be reordered")
        {
            const json source = R"([{"id": 1}, {"id": "1"}])"_json;
            const json target = R"([{"id": "1"}, {"id": 1}])"_json;

            check_roundtrip(source, target, root_ids);
        }

        SECTION("integer and floating-point numbers compare equal")
        {
            const json source = R"([{"id": 1, "v": 0}])"_json;
            const json target = R"([{"id": 1.0, "v": 1}])"_json;

            const json patch = json::keyed_diff(source, target, root_ids);
            CHECK(patch == json::array(
            {
                {{"op", "replace"}, {"path", "/0/v"}, {"value", 1}}
            }));
            CHECK(source.patch(patch) == target);
        }
    }

    SECTION("invalid keyed arrays reject the whole diff")
    {
        json patch;

        SECTION("missing id")
        {
            const json source = R"([{"id": "a"}, {"v": 1}])"_json;
            const json target = R"([{"id": "a"}])"_json;

            CHECK_THROWS_AS(patch = json::keyed_diff(source, target, root_ids), json::type_error&);
            CHECK_THROWS_WITH_AS(patch = json::keyed_diff(source, target, root_ids),
                                 "[json.exception.type_error.318] keyed_diff: object at '/1' has no id key 'id'", json::type_error&);
        }

        SECTION("duplicate id in source")
        {
            const json source = R"([{"id": "a"}, {"id": "a"}])"_json;
            const json target = R"([{"id": "a"}])"_json;

            CHECK_THROWS_AS(patch = json::keyed_diff(source, target, root_ids), json::type_error&);
            CHECK_THROWS_WITH_AS(patch = json::keyed_diff(source, target, root_ids),
                                 "[json.exception.type_error.318] keyed_diff: duplicate id '\"a\"' at '/1'", json::type_error&);
        }

        SECTION("duplicate id in target")
        {
            const json source = R"([{"id": "a"}])"_json;
            const json target = R"([{"id": "b"}, {"id": "b"}])"_json;

            CHECK_THROWS_WITH_AS(patch = json::keyed_diff(source, target, root_ids),
                                 "[json.exception.type_error.318] keyed_diff: duplicate id '\"b\"' at '/1'", json::type_error&);
        }

        SECTION("non-object element")
        {
            const json source = R"([{"id": "a"}, 5])"_json;
            const json target = R"([{"id": "a"}])"_json;

            CHECK_THROWS_WITH_AS(patch = json::keyed_diff(source, target, root_ids),
                                 "[json.exception.type_error.318] keyed_diff: element at '/1' is number, but elements of keyed arrays must be objects", json::type_error&);
        }

        SECTION("id of disallowed type")
        {
            const json source = R"([{"id": true}])"_json;
            const json target = R"([{"id": false}])"_json;

            CHECK_THROWS_WITH_AS(patch = json::keyed_diff(source, target, root_ids),
                                 "[json.exception.type_error.318] keyed_diff: id at '/0' is boolean, but ids must be strings or numbers", json::type_error&);
        }
    }

    SECTION("unconfigured arrays keep positional semantics")
    {
        const json source = R"({"items": [{"id": "a"}, {"id": "b"}, {"id": "c"}], "other": [1, 2, 3]})"_json;
        const json target = R"({"items": [{"id": "c"}, {"id": "a"}, {"id": "b"}], "other": [3, 2, 1]})"_json;

        // only a nonexistent path is configured, so all arrays are
        // compared positionally
        const std::map<json::string_t, json::string_t> keys {{"/nonexistent", "id"}};
        CHECK(json::keyed_diff(source, target, keys) == json::diff(source, target));

        // no configuration at all: identical to diff
        CHECK(json::keyed_diff(source, target) == json::diff(source, target));
        CHECK(json::keyed_diff(source, target, {}) == json::diff(source, target));
    }

    SECTION("configured path with non-array values is ignored")
    {
        const json source = R"({"id": 1})"_json;
        const json target = R"({"id": 2})"_json;

        CHECK(json::keyed_diff(source, target, root_ids) == json::diff(source, target));
    }

    SECTION("roundtrips")
    {
        check_roundtrip(R"([])"_json, R"([])"_json, root_ids);
        check_roundtrip(R"([{"id": "a"}, {"id": "b"}])"_json, R"([])"_json, root_ids);
        check_roundtrip(R"([])"_json, R"([{"id": "a"}, {"id": "b"}])"_json, root_ids);
        check_roundtrip(R"([{"id": "a"}, {"id": "b"}, {"id": "c"}, {"id": "d"}])"_json,
                        R"([{"id": "d"}, {"id": "x"}, {"id": "b"}, {"id": "a"}])"_json, root_ids);
        check_roundtrip(R"([{"id": 1, "n": {"id": "a"}}, {"id": 2}])"_json,
                        R"([{"id": 2, "n": {"id": "b"}}, {"id": 1}])"_json, root_ids);
        // equal values produce an empty patch
        CHECK(json::keyed_diff(R"([{"id": "a"}])"_json, R"([{"id": "a"}])"_json, root_ids).empty());
    }
}
