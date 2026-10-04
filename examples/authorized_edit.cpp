// Small example: explicit authorization layer for JSON editing.
//
// Build with -j1 (single-threaded, deterministic):
//   cd examples && make -j1
//
// Or compile directly:
//   g++ -std=c++11 -I include examples/authorized_edit.cpp -o examples/authorized_edit

#include <nlohmann/json_authorized_edit.hpp>
#include <iostream>

using nlohmann::json;
using nlohmann::authorized_json_editor;

int main()
{
    // The document to edit.
    json doc = R"({
        "users": [
            {"name": "alice", "role": "admin"},
            {"name": "bob", "role": "user"}
        ],
        "settings": {"theme": "dark"}
    })"_json;

    // The caller is only authorized to modify paths under /users.
    authorized_json_editor editor;
    editor.authorize("/users");

    // Submit a batch of set and remove operations.
    editor.set("/users/0/name", "alice");   // authorized: /users is a token-level prefix
    editor.set("/users/1/role", "admin");    // authorized
    editor.remove("/users/1");                // authorized: removes bob

    // Apply all operations transactionally: either all succeed or none are applied.
    auto result = editor.apply(doc);

    if (result.success)
    {
        std::cout << "All operations applied successfully.\n";
    }
    else
    {
        std::cout << "Operation " << result.failed_operation << " failed: " << result.reason << "\n";
        std::cout << "Original document is unchanged.\n";
    }

    std::cout << "Document:\n" << doc.dump(2) << "\n";

    // Now try an unauthorized operation: /settings is not under /users.
    authorized_json_editor editor2;
    editor2.authorize("/users");
    editor2.set("/settings/theme", "light");

    auto result2 = editor2.apply(doc);
    if (!result2.success)
    {
        std::cout << "\nUnauthorized edit correctly rejected.\n";
        std::cout << "Reason: " << result2.reason << "\n";
    }

    return 0;
}
