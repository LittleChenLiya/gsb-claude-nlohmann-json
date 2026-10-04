// JSON Patch preflight demo: dry-runs JSON Patches on an internal copy and
// prints a per-operation report (applied / failed / blocked).
//
// Build (from the repository root):
//     g++ -std=c++11 -I include patch_preflight/demo.cpp -o patch_preflight/demo
//
// SPDX-License-Identifier: MIT

#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "json_patch_preflight.hpp"

namespace preflight = nlohmann::patch_preflight;
using nlohmann::json;

static void print_report(const std::string& title,
                         const json& document_before,
                         const json& patch)
{
    const std::string document_dump = document_before.dump();
    const std::string patch_dump = patch.dump();

    const preflight::report rep = preflight::check(document_before, patch);

    std::cout << "=== " << title << " ===\n";
    std::cout << "document (before): " << document_dump << '\n';
    std::cout << "patch:             " << patch_dump << "\n\n";

    if (!rep.error.empty())
    {
        std::cout << "patch-level error: " << rep.error << '\n';
    }

    std::cout << " op#  op       status   pointer(s)                 diagnostic\n";
    std::cout << " ---  -------  -------  -------------------------  -----------\n";
    for (const auto& op : rep.operations)
    {
        std::string pointers = "path=" + (op.path.empty() ? std::string("\"\" (root)") : op.path);
        if (op.has_from)
        {
            pointers = "from=" + op.from + "  " + pointers;
        }

        std::cout << ' ' << op.index << "    "
                  << op.op;
        for (std::size_t i = op.op.size(); i < 8; ++i)
        {
            std::cout << ' ';
        }
        std::cout << ' ' << preflight::to_string(op.status);
        for (std::size_t i = std::string(preflight::to_string(op.status)).size(); i < 8; ++i)
        {
            std::cout << ' ';
        }
        std::cout << ' ' << pointers;
        for (std::size_t i = pointers.size(); i < 26; ++i)
        {
            std::cout << ' ';
        }

        if (op.status == preflight::op_status::failed)
        {
            std::cout << " [" << preflight::to_string(op.failed_at) << "] " << op.message;
        }
        else if (op.status == preflight::op_status::blocked)
        {
            std::cout << " not executed: an earlier operation failed";
        }
        std::cout << '\n';
    }
    std::cout << '\n';

    if (rep.ok)
    {
        std::cout << "preflight result: OK - all " << rep.operations.size()
                  << " operation(s) would apply\n";
    }
    else if (rep.error.empty())
    {
        std::cout << "preflight result: FAILED at operation " << rep.failed_index
                  << " (" << (rep.operations.size() - rep.failed_index - 1)
                  << " operation(s) blocked)\n";
    }
    std::cout << "document after applied prefix: " << rep.document.dump() << '\n';
    std::cout << "input document unchanged: " << (document_before.dump() == document_dump ? "yes" : "NO") << '\n';
    std::cout << "input patch unchanged:    " << (patch.dump() == patch_dump ? "yes" : "NO") << '\n';
    std::cout << '\n';
}

int main()
{
    // Scenario 1: replace /a succeeds, test /b fails, remove /c is blocked.
    // The input document keeps its old "a" value - only the internal copy
    // was modified.
    {
        const json document = {{"a", "old"}, {"b", 1}, {"c", json::array({1, 2, 3})}};
        const json patch = json::array({
            {{"op", "replace"}, {"path", "/a"}, {"value", "new"}},
            {{"op", "test"}, {"path", "/b"}, {"value", 42}},
            {{"op", "remove"}, {"path", "/c"}}
        });
        print_report("replace succeeds, test fails, remove is blocked", document, patch);
    }

    // Scenario 2: move with a missing "from" location - the failure is
    // attributed to the "from" pointer, not to "path".
    {
        const json document = {{"x", 1}};
        const json patch = json::array({
            {{"op", "move"}, {"from", "/missing"}, {"path", "/y"}},
            {{"op", "add"}, {"path", "/z"}, {"value", 3}}
        });
        print_report("move with missing 'from' location", document, patch);
    }

    // Scenario 3: a fully successful patch, including an array append via
    // the "-" index, a copy, and a test.
    {
        const json document = {{"items", json::array({1, 2})}, {"name", "demo"}};
        const json patch = json::array({
            {{"op", "add"}, {"path", "/items/-"}, {"value", 3}},
            {{"op", "copy"}, {"from", "/name"}, {"path", "/title"}},
            {{"op", "test"}, {"path", "/items/2"}, {"value", 3}}
        });
        print_report("successful patch (append, copy, test)", document, patch);
    }

    return 0;
}
