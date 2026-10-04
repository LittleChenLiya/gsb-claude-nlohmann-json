// Small example for the explicit authorization layer for JSON edits.
//
// Build and run (from the repository root):
//   c++ -std=c++11 -I include tests/authorized_edit/example_authorized_edit.cpp -o example_authorized_edit
//   ./example_authorized_edit
// or serially via make:
//   make -j1 -C tests/authorized_edit run-example

#include <iostream>
#include <nlohmann/authorized_edit.hpp>

using nlohmann::json;
namespace ae = nlohmann::authorized_edit;

int main()
{
    const json document = {
        {"users", json::array({
            {{"name", "ann"}, {"tags", json::array({"admin", "ops"})}},
            {{"name", "bob"}, {"tags", json::array({"guest"})}}
        })},
        {"users_backup", {{"note", "do not touch"}}}
    };

    // the caller explicitly authorizes only the "/users" subtree
    const ae::editor ed({"/users"});

    // batch 1: every operation is authorized -> the result is published
    const auto ok = ed.apply(document, {
        ae::operation::set("/users/0/name", "annie"),
        ae::operation::remove("/users/1/tags/0"), // shifts later indices
        ae::operation::set("/users/1/tags/-", "newbie")
    });
    std::cout << "batch 1 success: " << std::boolalpha << ok.success << '\n';
    if (ok.success)
    {
        std::cout << ok.document.dump(2) << '\n';
    }

    // batch 2: the second operation escapes the authorized prefix ->
    // nothing is published, the failure index and reason are reported
    const auto denied = ed.apply(document, {
        ae::operation::set("/users/0/name", "annie"),
        ae::operation::set("/users_backup/note", "hacked")
    });
    std::cout << "batch 2 success: " << denied.success
              << ", failed at operation " << denied.failed_index
              << ": " << denied.reason << '\n';
    std::cout << "original unchanged: " << (document.at("users_backup").at("note") == "do not touch") << '\n';
}
