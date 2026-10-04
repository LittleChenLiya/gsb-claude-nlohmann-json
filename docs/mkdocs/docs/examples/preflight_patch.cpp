#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace nlohmann::literals;

int main()
{
    // the original document
    json doc = R"(
        {
          "a": "old",
          "b": 2
        }
    )"_json;

    // the patch: the first operation succeeds, the second fails,
    // and the third is blocked because the preflight does not continue
    // on an uncertain state
    json patch = R"(
        [
          { "op": "replace", "path": "/a", "value": "new" },
          { "op": "test", "path": "/b", "value": 3 },
          { "op": "remove", "path": "/a" }
        ]
    )"_json;

    // preflight the patch; the original document and patch are not modified
    auto report = doc.preflight_patch(patch);

    // print the report
    std::cout << "Preflight report:\n";
    std::cout << std::string(60, '-') << '\n';
    for (const auto& op : report.operations)
    {
        std::cout << "[" << op.index << "] " << std::setw(8) << std::left << op.op;
        std::cout << "  path=" << op.path;
        if (!op.from.empty())
        {
            std::cout << "  from=" << op.from;
        }
        std::cout << "  status=";
        switch (op.status)
        {
            case nlohmann::json_patch_preflight_status::ok:
                std::cout << "ok";
                break;
            case nlohmann::json_patch_preflight_status::failed:
                std::cout << "failed";
                break;
            case nlohmann::json_patch_preflight_status::blocked:
                std::cout << "blocked";
                break;
        }
        if (!op.message.empty())
        {
            std::cout << "\n    message: " << op.message;
        }
        std::cout << '\n';
    }
    std::cout << std::string(60, '-') << '\n';
    std::cout << "overall: " << (report.ok() ? "ok" : "failed") << '\n';

    // the original document is unchanged
    std::cout << "\nOriginal document (unchanged):\n"
              << std::setw(4) << doc << '\n';
}
