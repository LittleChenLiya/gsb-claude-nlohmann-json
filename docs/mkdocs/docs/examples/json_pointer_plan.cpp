#include <cstddef>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
using plan_type = nlohmann::json_pointer_plan<json>;

const char* to_string(plan_type::status s)
{
    switch (s)
    {
        case plan_type::status::value:
            return "value";
        case plan_type::status::null_value:
            return "null_value";
        case plan_type::status::missing_key:
            return "missing_key";
        case plan_type::status::wrong_container:
            return "wrong_container";
        case plan_type::status::array_out_of_bounds:
            return "array_out_of_bounds";
    }
    return "?";
}
} // namespace

int main()
{
    json document =
    {
        {"a", {{"x", 1}, {"y", 2}, {"n", nullptr}}},
        {"list", {10, 20}}
    };

    // compile a batch plan: common prefixes are merged into one traversal tree
    plan_type plan;
    plan.add("/a/x");
    plan.add("/a/y");
    plan.add("/a/n");
    plan.add("/a/missing");
    plan.add("/a/missing/deeper");
    plan.add("/list/5");
    plan.add("/a/x"); // duplicate query: its identity is preserved

    std::cout << "requests: " << plan.request_count() << ", unique: " << plan.unique_count() << '\n';
    std::cout << "merged prefixes:";
    for (const auto& prefix : plan.prefixes())
    {
        std::cout << " \"" << prefix << '"';
    }
    std::cout << '\n';

    // execute the plan: every shared prefix is accessed exactly once,
    // and the document is only read, never modified
    const std::string before = document.dump();
    plan_type::statistics stats;
    auto results = plan.execute(document, &stats);

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const auto& res = results[i];
        std::cout << res.pointer << " -> " << to_string(res.outcome);
        if (res.ok())
        {
            std::cout << ' ' << res.value.dump();
        }
        else
        {
            std::cout << " [" << res.error_source->prefix << ": " << res.error_source->message << ']';
        }
        if (res.canonical != i)
        {
            std::cout << " (duplicate of #" << res.canonical << ')';
        }
        std::cout << '\n';
    }

    // "/a" was accessed once although five requests pass through it
    std::cout << "accesses to \"/a\": " << stats.prefix_visits.at("/a") << '\n';
    std::cout << "nodes visited: " << stats.nodes_visited << '\n';

    // both requests below the missing prefix share the same error source
    std::cout << "shared error source: " << std::boolalpha
              << (results[3].error_source == results[4].error_source) << '\n';

    // execute() never inserts missing keys into the document
    std::cout << "document unchanged by execute: " << (document.dump() == before) << '\n';

    // results are independently owned: later changes to the document do not affect them
    document["a"]["x"] = 99;
    std::cout << "after modifying the document, the \"/a/x\" result is still " << results[0].value.dump() << '\n';
}
