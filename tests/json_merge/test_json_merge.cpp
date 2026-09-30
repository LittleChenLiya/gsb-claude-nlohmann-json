// Standalone acceptance tests for the three-way merge component.
// Build:  g++ -std=c++11 -I include tests/json_merge/test_json_merge.cpp -o test_json_merge
// Run:    ./test_json_merge

#include <cstdlib>
#include <iostream>
#include <string>

#include <nlohmann/json_merge.hpp>

namespace
{

int failures = 0;

#define CHECK(cond)                                                        \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            ++failures;                                                    \
            std::cerr << "FAILED " << __FILE__ << ':' << __LINE__          \
                      << ": " #cond << std::endl;                          \
        }                                                                  \
    } while (false)

using nlohmann::json;
using nlohmann::json_merge_options;
using nlohmann::three_way_merge;

void test_object_distinct_fields()
{
    const json base = {{"a", 1}, {"b", 2}, {"keep", true}};
    const json local = {{"a", 10}, {"b", 2}, {"keep", true}};
    const json incoming = {{"a", 1}, {"b", 20}, {"keep", true}};

    const auto result = three_way_merge(base, local, incoming);
    CHECK(result.success());
    CHECK(result.merged["a"] == 10);
    CHECK(result.merged["b"] == 20);
    CHECK(result.merged["keep"] == true);
}

void test_same_value_modification()
{
    const json base = {{"a", 1}};
    const json local = {{"a", 42}};
    const json incoming = {{"a", 42}};

    const auto result = three_way_merge(base, local, incoming);
    CHECK(result.success());
    CHECK(result.merged["a"] == 42);
}

void test_id_array_distinct_elements()
{
    const json base = {{"items", {{{"id", "x"}, {"v", 1}}, {{"id", "y"}, {"v", 2}}}}};
    const json local = {{"items", {{{"id", "x"}, {"v", 100}}, {{"id", "y"}, {"v", 2}}}}};
    const json incoming = {{"items", {{{"id", "x"}, {"v", 1}}, {{"id", "y"}, {"v", 200}}}}};

    json_merge_options options;
    options.id_fields["/items"] = "id";
    const auto result = three_way_merge(base, local, incoming, options);
    CHECK(result.success());
    CHECK(result.merged["items"].size() == 2);
    CHECK(result.merged["items"][0]["id"] == "x");
    CHECK(result.merged["items"][0]["v"] == 100);
    CHECK(result.merged["items"][1]["id"] == "y");
    CHECK(result.merged["items"][1]["v"] == 200);
}

void test_delete_vs_modify_conflict()
{
    const json base = {{"a", 1}, {"b", 2}};
    const json local = {{"a", 1}};              // local deleted "b"
    const json incoming = {{"a", 1}, {"b", 20}}; // incoming modified "b"

    const auto result = three_way_merge(base, local, incoming);
    CHECK(!result.success());
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].pointer.to_string() == "/b");
    CHECK(result.conflicts[0].base_present);
    CHECK(!result.conflicts[0].local_present);
    CHECK(result.conflicts[0].incoming_present);
    CHECK(result.conflicts[0].incoming_value == 20);
    // conflict position must be addressable through the merged document
    CHECK(result.merged.at(result.conflicts[0].pointer) == 20);
}

void test_both_add_same_id_different_values()
{
    const json base = {{"items", json::array()}};
    const json local = {{"items", {{{"id", "n"}, {"v", "local"}}}}};
    const json incoming = {{"items", {{{"id", "n"}, {"v", "incoming"}}}}};

    json_merge_options options;
    options.id_fields["/items"] = "id";
    const auto result = three_way_merge(base, local, incoming, options);
    CHECK(!result.success());
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].pointer.to_string() == "/items/n");
    CHECK(result.merged["items"].size() == 1);
    CHECK(result.merged["items"][0]["v"] == "local");
}

void test_duplicate_id()
{
    const json base = {{"items", json::array()}};
    const json local = {{"items", {{{"id", "x"}, {"v", 1}}, {{"id", "x"}, {"v", 2}}}}};
    const json incoming = {{"items", {{{"id", "y"}, {"v", 3}}}}};

    json_merge_options options;
    options.id_fields["/items"] = "id";
    const auto result = three_way_merge(base, local, incoming, options);
    CHECK(!result.success());
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].pointer.to_string() == "/items/x");
    CHECK(result.conflicts[0].reason == "duplicate id in array");
}

void test_root_value_replacement()
{
    const auto result = three_way_merge(json(1), json(2), json(3));
    CHECK(!result.success());
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].pointer.to_string() == "");
    CHECK(result.conflicts[0].reason == "modified on both sides");
    CHECK(result.merged == 2); // deterministic fallback: local

    // root replaced on only one side merges cleanly
    const auto one_sided = three_way_merge(json(1), json({{"a", 1}}), json(1));
    CHECK(one_sided.success());
    CHECK(one_sided.merged == json({{"a", 1}}));
}

void test_plain_array_is_opaque()
{
    const json base = {{"tags", {"a", "b"}}};
    const json local = {{"tags", {"a", "c"}}};
    const json incoming = {{"tags", {"a", "d"}}};

    const auto result = three_way_merge(base, local, incoming);
    CHECK(!result.success());
    CHECK(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].pointer.to_string() == "/tags");
    CHECK(result.merged["tags"] == json({"a", "c"}));

    // one-sided array change merges as a whole
    const auto one_sided = three_way_merge(base, local, base);
    CHECK(one_sided.success());
    CHECK(one_sided.merged["tags"] == json({"a", "c"}));
}

void test_escaped_pointer()
{
    // keys containing '~' and '/' require ~0/~1 escaping in JSON Pointers
    const json base = {{"a/b", {{"~key", 1}, {"other", 2}}}};
    const json local = {{"a/b", {{"~key", 10}, {"other", 2}}}};
    const json incoming = {{"a/b", {{"~key", 1}, {"other", 20}}}};

    const auto result = three_way_merge(base, local, incoming);
    CHECK(result.success());
    CHECK(result.merged["a/b"]["~key"] == 10);
    CHECK(result.merged["a/b"]["other"] == 20);

    // id arrays under escaped pointers must be found via the escaped form
    const json arr_base = {{"a/b", {{{"id", "i"}, {"v", 1}}}}};
    const json arr_local = {{"a/b", {{{"id", "i"}, {"v", 7}}}}};
    const json arr_incoming = {{"a/b", {{{"id", "i"}, {"v", 1}}, {{"id", "j"}, {"v", 9}}}}};

    json_merge_options options;
    options.id_fields["/a~1b"] = "id";
    const auto arr_result = three_way_merge(arr_base, arr_local, arr_incoming, options);
    CHECK(arr_result.success());
    CHECK(arr_result.merged["a/b"].size() == 2);
    CHECK(arr_result.merged["a/b"][0]["v"] == 7);
    CHECK(arr_result.merged["a/b"][1]["id"] == "j");
}

void test_id_array_order_and_append()
{
    const json base = {{"items", {{{"id", "a"}}, {{"id", "b"}}, {{"id", "c"}}}}};
    // local reorders unchanged elements and adds "l1"
    const json local = {{"items", {{{"id", "c"}}, {{"id", "a"}}, {{"id", "b"}}, {{"id", "l1"}}}}};
    // incoming deletes "b" and adds "i1"
    const json incoming = {{"items", {{{"id", "a"}}, {{"id", "c"}}, {{"id", "i1"}}}}};

    json_merge_options options;
    options.id_fields["/items"] = "id";
    const auto result = three_way_merge(base, local, incoming, options);
    CHECK(result.success());
    // unchanged elements keep base order (minus deleted "b");
    // new elements appended: local additions first, then incoming
    CHECK(result.merged["items"] == json({{{"id", "a"}}, {{"id", "c"}}, {{"id", "l1"}}, {{"id", "i1"}}}));
}

void test_conflict_order_is_stable()
{
    const json base = {{"m", 1}, {"z", 2}, {"a", 3}};
    const json local = {{"m", 10}, {"z", 20}, {"a", 30}};
    const json incoming = {{"m", 100}, {"z", 200}, {"a", 300}};

    const auto first = three_way_merge(base, local, incoming);
    const auto second = three_way_merge(base, local, incoming);
    CHECK(first.conflicts.size() == 3);
    // conflicts are reported in deterministic sorted-pointer order
    CHECK(first.conflicts[0].pointer.to_string() == "/a");
    CHECK(first.conflicts[1].pointer.to_string() == "/m");
    CHECK(first.conflicts[2].pointer.to_string() == "/z");
    CHECK(first.merged == json({{"m", 10}, {"z", 20}, {"a", 30}}));
    // repeated runs produce identical results
    CHECK(first.merged == second.merged);
    CHECK(first.conflicts.size() == second.conflicts.size());
    for (std::size_t i = 0; i < first.conflicts.size(); ++i)
    {
        CHECK(first.conflicts[i].pointer.to_string() == second.conflicts[i].pointer.to_string());
    }
}

void test_nested_and_untouched()
{
    const json base = {{"outer", {{"inner", 1}, {"gone", 2}}}, {"same", "x"}};
    const json local = {{"outer", {{"inner", 1}, {"gone", 2}}}, {"same", "x"}};
    const json incoming = {{"outer", {{"inner", 5}}}, {"same", "x"}}; // modified inner, deleted gone

    const auto result = three_way_merge(base, local, incoming);
    CHECK(result.success());
    CHECK(result.merged == incoming);
}

} // namespace

int main()
{
    test_object_distinct_fields();
    test_same_value_modification();
    test_id_array_distinct_elements();
    test_delete_vs_modify_conflict();
    test_both_add_same_id_different_values();
    test_duplicate_id();
    test_root_value_replacement();
    test_plain_array_is_opaque();
    test_escaped_pointer();
    test_id_array_order_and_append();
    test_conflict_order_is_stable();
    test_nested_and_untouched();

    if (failures != 0)
    {
        std::cerr << failures << " check(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "all tests passed" << std::endl;
    return EXIT_SUCCESS;
}
