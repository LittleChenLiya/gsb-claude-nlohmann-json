//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
//  |  |  |__   |  |  | | | |  version 3.12.0
//  |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
//  SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
//  SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/merge.hpp>
using nlohmann::json;
#ifdef JSON_TEST_NO_GLOBAL_UDLS
    using namespace nlohmann::literals; // NOLINT(google-build-using-namespace)
#endif

namespace
{
std::vector<std::string> conflict_locations(const nlohmann::merge_result& result)
{
    std::vector<std::string> locations;
    for (const auto& conflict : result.conflicts)
    {
        locations.push_back(conflict.pointer.to_string());
    }
    return locations;
}
} // namespace

TEST_CASE("three-way merge")
{
    SECTION("object: different fields modified merge cleanly")
    {
        const json base = {{"a", 1}, {"b", 2}, {"c", 3}};
        const json local = {{"a", 10}, {"b", 2}, {"c", 3}};
        const json incoming = {{"a", 1}, {"b", 20}, {"c", 3}};

        const auto result = nlohmann::merge3(base, local, incoming);
        CHECK_FALSE(result.has_conflicts());
        CHECK(result.merged == json({{"a", 10}, {"b", 20}, {"c", 3}}));
    }

    SECTION("object: nested different fields modified merge cleanly")
    {
        const json base = {{"outer", {{"x", 1}, {"y", 2}}}};
        const json local = {{"outer", {{"x", 100}, {"y", 2}}}};
        const json incoming = {{"outer", {{"x", 1}, {"y", 200}}}};

        const auto result = nlohmann::merge3(base, local, incoming);
        CHECK_FALSE(result.has_conflicts());
        CHECK(result.merged == json({{"outer", {{"x", 100}, {"y", 200}}}}));
    }

    SECTION("object: identical change on both sides merges cleanly")
    {
        const json base = {{"a", 1}};
        const json local = {{"a", 5}};
        const json incoming = {{"a", 5}};

        const auto result = nlohmann::merge3(base, local, incoming);
        CHECK_FALSE(result.has_conflicts());
        CHECK(result.merged == json({{"a", 5}}));
    }

    SECTION("object: delete vs modify conflict is reported at its location")
    {
        const json base = {{"keep", 1}, {"doomed", 2}};
        const json local = {{"keep", 1}};                    // deletes "doomed"
        const json incoming = {{"keep", 1}, {"doomed", 42}}; // modifies "doomed"

        const auto result = nlohmann::merge3(base, local, incoming);
        REQUIRE(result.conflicts.size() == 1);
        CHECK(result.conflicts[0].kind == nlohmann::merge_conflict_kind::delete_vs_modify);
        CHECK(result.conflicts[0].pointer == json::json_pointer("/doomed"));
        CHECK(result.conflicts[0].local_deleted);
        CHECK_FALSE(result.conflicts[0].incoming_deleted);
        CHECK(result.conflicts[0].incoming == 42);
        // deletion wins deterministically
        CHECK(result.merged == json({{"keep", 1}}));
    }

    SECTION("object: modify vs delete conflict is reported at its location")
    {
        const json base = {{"keep", 1}, {"doomed", 2}};
        const json local = {{"keep", 1}, {"doomed", 42}}; // modifies "doomed"
        const json incoming = {{"keep", 1}};              // deletes "doomed"

        const auto result = nlohmann::merge3(base, local, incoming);
        REQUIRE(result.conflicts.size() == 1);
        CHECK(result.conflicts[0].kind == nlohmann::merge_conflict_kind::delete_vs_modify);
        CHECK(result.conflicts[0].pointer == json::json_pointer("/doomed"));
        CHECK(result.conflicts[0].incoming_deleted);
        CHECK(result.conflicts[0].local == 42);
        CHECK(result.merged == json({{"keep", 1}}));
    }

    SECTION("object: delete vs unmodified is not a conflict")
    {
        const json base = {{"keep", 1}, {"gone", 2}};
        const json local = {{"keep", 1}};
        const json incoming = {{"keep", 1}, {"gone", 2}};

        const auto result = nlohmann::merge3(base, local, incoming);
        CHECK_FALSE(result.has_conflicts());
        CHECK(result.merged == json({{"keep", 1}}));
    }

    SECTION("id array: different elements modified merge cleanly")
    {
        nlohmann::merge_options options;
        options.id_fields[json::json_pointer("/items")] = "id";

        const json base = {{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "b"}}}}};
        const json local = {{"items", {{{"id", 1}, {"v", "a-local"}}, {{"id", 2}, {"v", "b"}}}}};
        const json incoming = {{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "b-incoming"}}}}};

        const auto result = nlohmann::merge3(base, local, incoming, options);
        CHECK_FALSE(result.has_conflicts());
        // base element order is preserved
        CHECK(result.merged == json({{"items", {{{"id", 1}, {"v", "a-local"}}, {{"id", 2}, {"v", "b-incoming"}}}}}));
    }

    SECTION("id array: new elements are appended local first, then incoming")
    {
        nlohmann::merge_options options;
        options.id_fields[json::json_pointer("/items")] = "id";

        const json base = {{"items", {{{"id", 1}, {"v", "a"}}}}};
        const json local = {{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "from-local"}}}}};
        const json incoming = {{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 3}, {"v", "from-incoming"}}}}};

        const auto result = nlohmann::merge3(base, local, incoming, options);
        CHECK_FALSE(result.has_conflicts());
        CHECK(result.merged == json({{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "from-local"}}, {{"id", 3}, {"v", "from-incoming"}}}}}));
    }

    SECTION("id array: both sides add the same id with different values")
    {
        nlohmann::merge_options options;
        options.id_fields[json::json_pointer("/items")] = "id";

        const json base = {{"items", json::array()}};
        const json local = {{"items", {{{"id", 9}, {"v", "local"}}}}};
        const json incoming = {{"items", {{{"id", 9}, {"v", "incoming"}}}}};

        const auto result = nlohmann::merge3(base, local, incoming, options);
        REQUIRE(result.conflicts.size() == 1);
        CHECK(result.conflicts[0].kind == nlohmann::merge_conflict_kind::add_vs_add);
        CHECK(result.conflicts[0].pointer == json::json_pointer("/items/9"));
        // local wins deterministically
        CHECK(result.merged == json({{"items", {{{"id", 9}, {"v", "local"}}}}}));
    }

    SECTION("id array: duplicate ids are reported")
    {
        nlohmann::merge_options options;
        options.id_fields[json::json_pointer("/items")] = "id";

        const json base = {{"items", {{{"id", 1}, {"v", "a"}}}}, {"other", 1}};
        const json local = {{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "x"}}, {{"id", 2}, {"v", "y"}}}}, {"other", 1}};
        const json incoming = {{"items", {{{"id", 1}, {"v", "a"}}}}, {"other", 2}};

        const auto result = nlohmann::merge3(base, local, incoming, options);
        REQUIRE(result.conflicts.size() == 1);
        CHECK(result.conflicts[0].kind == nlohmann::merge_conflict_kind::duplicate_id);
        CHECK(result.conflicts[0].pointer == json::json_pointer("/items/2"));
        // the first occurrence of the duplicated id is kept
        CHECK(result.merged == json({{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "x"}}}}, {"other", 2}}));
    }

    SECTION("id array: conflicting modification inside an element is located by id")
    {
        nlohmann::merge_options options;
        options.id_fields[json::json_pointer("/items")] = "id";

        const json base = {{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "b"}}}}};
        const json local = {{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "b-local"}}}}};
        const json incoming = {{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "b-incoming"}}}}};

        const auto result = nlohmann::merge3(base, local, incoming, options);
        REQUIRE(result.conflicts.size() == 1);
        CHECK(result.conflicts[0].kind == nlohmann::merge_conflict_kind::modify_vs_modify);
        CHECK(result.conflicts[0].pointer == json::json_pointer("/items/2/v"));
        CHECK(result.merged == json({{"items", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "b-local"}}}}}));
    }

    SECTION("root value replacement conflicts at the empty pointer")
    {
        const json base = 1;
        const json local = "text";
        const json incoming = json::array({1, 2});

        const auto result = nlohmann::merge3(base, local, incoming);
        REQUIRE(result.conflicts.size() == 1);
        CHECK(result.conflicts[0].kind == nlohmann::merge_conflict_kind::modify_vs_modify);
        CHECK(result.conflicts[0].pointer == json::json_pointer(""));
        CHECK(result.conflicts[0].pointer.to_string().empty());
        CHECK(result.merged == json("text")); // local wins
    }

    SECTION("root value replaced on one side only merges cleanly")
    {
        const json base = {{"a", 1}};
        const json local = json::array({1, 2, 3});
        const json incoming = {{"a", 1}};

        const auto result = nlohmann::merge3(base, local, incoming);
        CHECK_FALSE(result.has_conflicts());
        CHECK(result.merged == json::array({1, 2, 3}));
    }

    SECTION("plain array: treated as one atomic value")
    {
        const json base = {{"arr", {1, 2}}};
        const json local = {{"arr", {1, 2, 3}}};
        const json incoming = {{"arr", {1, 2, 4}}};

        const auto result = nlohmann::merge3(base, local, incoming);
        REQUIRE(result.conflicts.size() == 1);
        CHECK(result.conflicts[0].kind == nlohmann::merge_conflict_kind::modify_vs_modify);
        CHECK(result.conflicts[0].pointer == json::json_pointer("/arr"));
        CHECK(result.merged == json({{"arr", {1, 2, 3}}})); // local wins
    }

    SECTION("plain array: changed on one side only merges cleanly")
    {
        const json base = {{"arr", {1, 2}}};
        const json local = {{"arr", {1, 2}}};
        const json incoming = {{"arr", {3, 4}}};

        const auto result = nlohmann::merge3(base, local, incoming);
        CHECK_FALSE(result.has_conflicts());
        CHECK(result.merged == json({{"arr", {3, 4}}}));
    }

    SECTION("id array behind an escaped JSON pointer")
    {
        nlohmann::merge_options options;
        options.id_fields[json::json_pointer("/a~1b/~0c")] = "id";

        const json base = {{"a/b", {{"~c", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "b"}}}}}}};
        const json local = {{"a/b", {{"~c", {{{"id", 1}, {"v", "a-local"}}, {{"id", 2}, {"v", "b"}}}}}}};
        const json incoming = {{"a/b", {{"~c", {{{"id", 1}, {"v", "a"}}, {{"id", 2}, {"v", "b-incoming"}}}}}}};

        const auto result = nlohmann::merge3(base, local, incoming, options);
        CHECK_FALSE(result.has_conflicts());
        CHECK(result.merged == json({{"a/b", {{"~c", {{{"id", 1}, {"v", "a-local"}}, {{"id", 2}, {"v", "b-incoming"}}}}}}}));
    }

    SECTION("conflict order is stable")
    {
        const json base = {{"a", 1}, {"b", 2}, {"c", 3}};
        const json local = {{"b", 2}, {"c", 30}};          // deletes "a", modifies "c"
        const json incoming = {{"a", 10}, {"b", 2}, {"c", 31}}; // modifies "a" and "c"

        const auto first = nlohmann::merge3(base, local, incoming);
        const auto second = nlohmann::merge3(base, local, incoming);

        REQUIRE(first.conflicts.size() == 2);
        // conflicts follow document order: base keys first
        CHECK(conflict_locations(first) == std::vector<std::string>{"/a", "/c"});
        CHECK(first.conflicts[0].kind == nlohmann::merge_conflict_kind::delete_vs_modify);
        CHECK(first.conflicts[1].kind == nlohmann::merge_conflict_kind::modify_vs_modify);
        // repeated merges produce the identical conflict sequence
        CHECK(conflict_locations(first) == conflict_locations(second));
        CHECK(first.merged == second.merged);
    }
}
