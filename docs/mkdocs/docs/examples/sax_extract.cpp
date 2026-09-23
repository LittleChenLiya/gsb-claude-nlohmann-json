#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

int main()
{
    // a document with a large unselected middle and one selected value past it
    std::string document = R"({"meta":{"id":7},"payload":[)";
    for (int i = 0; i < 1000; ++i)
    {
        document += (i == 0) ? "" : ",";
        document += R"({"ignore":"me"})";
    }
    document += R"(],"x/y":{"~k":3}})";

    // only the listed subtrees are materialized; everything else is
    // validated but never stored
    const std::vector<std::string> pointers = {"/meta", "/x~1y/~0k", "/missing"};

    const auto result = nlohmann::sax_extract(document, pointers,
                        [](const std::string & pointer, nlohmann::json value)
    {
        std::cout << pointer << " -> " << value.dump() << '\n';
        return true; // return false to stop the extraction early
    });

    std::cout << std::boolalpha
              << "emitted: " << result.emitted << '\n'
              << "complete: " << result.complete << '\n'
              << "unmatched:";
    for (const auto& pointer : result.unmatched)
    {
        std::cout << ' ' << pointer;
    }
    std::cout << '\n';
}

