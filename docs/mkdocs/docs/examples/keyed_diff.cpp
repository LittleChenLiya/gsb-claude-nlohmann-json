#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace nlohmann::literals;

int main()
{
    // the source document: an array of objects with unique ids
    json source = R"(
        [
            {"id": "a", "value": 1},
            {"id": "b", "value": 2},
            {"id": "c", "value": 3}
        ]
    )"_json;

    // the target document: "c" moved to the front and changed,
    // "b" removed, "d" added
    json target = R"(
        [
            {"id": "c", "value": 30},
            {"id": "a", "value": 1},
            {"id": "d", "value": 4}
        ]
    )"_json;

    // create the patch, matching elements of the root array by their "id" member
    json patch = json::keyed_diff(source, target, {{"", "id"}});

    // roundtrip: apply the patch to the source
    json patched_source = source.patch(patch);

    // output patch, roundtrip result, and whether it equals the target
    std::cout << std::setw(4) << patch << "\n\n"
              << std::setw(4) << patched_source << "\n\n"
              << std::boolalpha << (patched_source == target) << std::endl;
}
