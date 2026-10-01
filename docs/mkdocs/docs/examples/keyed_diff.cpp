#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace nlohmann::literals;

int main()
{
    // the source document: an array of objects with an "id" member
    json source = R"(
        [
            {"id": "a", "value": 1},
            {"id": "b", "value": 2},
            {"id": "c", "value": 3}
        ]
    )"_json;

    // the target document: "c" was moved to the front and changed,
    // "b" was removed, and "d" was added
    json target = R"(
        [
            {"id": "c", "value": 42},
            {"id": "a", "value": 1},
            {"id": "d", "value": 4}
        ]
    )"_json;

    // create the patch, matching the array elements by their "id" member
    json patch = json::keyed_diff(source, target, {{"", "id"}});

    // roundtrip
    json patched_source = source.patch(patch);

    // output patch and roundtrip result
    std::cout << std::setw(4) << patch << "\n\n"
              << std::setw(4) << patched_source << std::endl;
}
