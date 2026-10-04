//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/json_authorized_edit.hpp>
using nlohmann::json;
using nlohmann::authorized_json_editor;

TEST_CASE("JSON authorized edit")
{
    SECTION("basic authorization")
    {
        // only /a is authorized; modifying /a/0/name should succeed
        json doc = R"({"a": [{"name": "old"}], "a2": 1})"_json;

        authorized_json_editor editor;
        editor.authorize("/a");
        editor.set("/a/0/name", "new");

        auto result = editor.apply(doc);
        CHECK(result.success);
        CHECK(doc["a"][0]["name"] == "new");

        // modifying /a2 should fail: /a is not a token-level prefix of /a2
        json doc2 = R"({"a": [{"name": "old"}], "a2": 1})"_json;

        authorized_json_editor editor2;
        editor2.authorize("/a");
        editor2.set("/a2", 2);

        auto result2 = editor2.apply(doc2);
        CHECK_FALSE(result2.success);
        CHECK(result2.failed_operation == 0);
        // original must be unchanged
        CHECK(doc2["a2"] == 1);
    }

    SECTION("slash in key is a single token")
    {
        // /a~1b decodes to the single key "a/b", not two layers
        json doc = R"({"a/b": 1, "a": {"b": 2}})"_json;

        authorized_json_editor editor;
        editor.authorize("/a~1b");
        editor.set("/a~1b", 100);

        auto result = editor.apply(doc);
        CHECK(result.success);
        CHECK(doc["a/b"] == 100);
        // the nested /a/b should be unchanged
        CHECK(doc["a"]["b"] == 2);
    }

    SECTION("transactional: failure leaves original unchanged")
    {
        json doc = R"({"a": [1, 2, 3], "b": 1})"_json;

        authorized_json_editor editor;
        editor.authorize("/a");
        editor.set("/a/0", 100);  // should succeed
        editor.set("/b", 2);       // should fail: not authorized

        auto result = editor.apply(doc);
        CHECK_FALSE(result.success);
        CHECK(result.failed_operation == 1);
        // original must be completely unchanged
        CHECK(doc["a"][0] == 1);
        CHECK(doc["b"] == 1);
    }

    SECTION("array deletion causes subsequent index changes")
    {
        // removing /a/0 shifts elements: old index 1 becomes new index 0
        json doc = R"({"a": [10, 20, 30]})"_json;

        authorized_json_editor editor;
        editor.authorize("/a");
        editor.remove("/a/0");      // remove first element -> [20, 30]
        editor.set("/a/0", 100);    // set first element to 100 -> [100, 30]

        auto result = editor.apply(doc);
        CHECK(result.success);
        CHECK(doc["a"].size() == 2);
        CHECK(doc["a"][0] == 100);
        CHECK(doc["a"][1] == 30);
    }

    SECTION("parent deletion requires parent permission")
    {
        // only /a/b is authorized; removing /a should fail
        json doc = R"({"a": {"b": {"c": 1}}})"_json;

        authorized_json_editor editor;
        editor.authorize("/a/b");
        editor.remove("/a");

        auto result = editor.apply(doc);
        CHECK_FALSE(result.success);
        // original must be unchanged
        CHECK(doc["a"]["b"]["c"] == 1);

        // with /a authorized, removing /a should succeed
        json doc2 = R"({"a": {"b": {"c": 1}}})"_json;

        authorized_json_editor editor2;
        editor2.authorize("/a");
        editor2.remove("/a");

        auto result2 = editor2.apply(doc2);
        CHECK(result2.success);
        CHECK(doc2 == R"({})"_json);
    }

    SECTION("array index vs object string key")
    {
        // /a/0 where a is an array: 0 is an array index
        json doc = R"({"a": [1, 2, 3], "b": {"0": "x"}})"_json;

        authorized_json_editor editor;
        editor.authorize("/a/0");
        editor.remove("/a/0");

        auto result = editor.apply(doc);
        CHECK(result.success);
        CHECK(doc["a"].size() == 2);
        CHECK(doc["a"][0] == 2);

        // /b/0 where b is an object: "0" is a string key
        // having /a/0 authorized must not allow touching /b/0
        json doc2 = R"({"a": [1, 2, 3], "b": {"0": "x"}})"_json;

        authorized_json_editor editor2;
        editor2.authorize("/a/0");
        editor2.remove("/b/0");

        auto result2 = editor2.apply(doc2);
        CHECK_FALSE(result2.success);
        // original must be unchanged
        CHECK(doc2["b"]["0"] == "x");
    }

    SECTION("multiple authorized prefixes")
    {
        json doc = R"({"a": 1, "b": 2, "c": 3})"_json;

        authorized_json_editor editor;
        editor.authorize("/a");
        editor.authorize("/b");
        editor.set("/a", 10);
        editor.set("/b", 20);

        auto result = editor.apply(doc);
        CHECK(result.success);
        CHECK(doc["a"] == 10);
        CHECK(doc["b"] == 20);
        CHECK(doc["c"] == 3);  // unchanged
    }

    SECTION("exact prefix match")
    {
        // authorizing /a should allow setting /a exactly
        json doc = R"({"a": 1})"_json;

        authorized_json_editor editor;
        editor.authorize("/a");
        editor.set("/a", 100);

        auto result = editor.apply(doc);
        CHECK(result.success);
        CHECK(doc["a"] == 100);
    }

    SECTION("remove nonexistent path fails")
    {
        json doc = R"({"a": 1})"_json;

        authorized_json_editor editor;
        editor.authorize("/a");
        editor.remove("/a/b");  // a is not an object

        auto result = editor.apply(doc);
        CHECK_FALSE(result.success);
        CHECK(result.failed_operation == 0);
        // original must be unchanged
        CHECK(doc["a"] == 1);
    }

    SECTION("set on array with out-of-range index fails")
    {
        json doc = R"({"a": [1, 2]})"_json;

        authorized_json_editor editor;
        editor.authorize("/a");
        editor.set("/a/5", 100);  // index 5 is out of range

        auto result = editor.apply(doc);
        CHECK_FALSE(result.success);
        // original must be unchanged
        CHECK(doc["a"].size() == 2);
    }
}
