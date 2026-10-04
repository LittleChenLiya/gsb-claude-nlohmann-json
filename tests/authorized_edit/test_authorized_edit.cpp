// Targeted tests for the explicit authorization layer for JSON edits
// (include/nlohmann/authorized_edit.hpp).
//
// Build and run (from the repository root):
//   c++ -std=c++11 -Wall -Wextra -I include tests/authorized_edit/test_authorized_edit.cpp -o test_authorized_edit
//   ./test_authorized_edit
// or serially via make:
//   make -j1 -C tests/authorized_edit run-test

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/authorized_edit.hpp>

using nlohmann::json;
namespace ae = nlohmann::authorized_edit;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__            \
                      << ": CHECK(" #cond ")\n";                           \
        }                                                                  \
    } while (0)

static void test_prefix_authorization_token_wise()
{
    std::cerr << "== token-wise prefix authorization ==\n";
    ae::editor ed({"/a"});

    const json original = {
        {"a", json::array({{{"name", "n0"}}, {{"name", "n1"}}})},
        {"a2", "keep"}
    };

    // "/a" authorizes "/a/0/name"
    {
        const json before = original;
        auto res = ed.apply(original, {ae::operation::set("/a/0/name", "changed")});
        CHECK(res.success);
        CHECK(res.document["a"][0]["name"] == "changed");
        CHECK(res.document["a"][1]["name"] == "n1");
        CHECK(res.document["a2"] == "keep");
        CHECK(original == before); // original untouched
    }

    // "/a" does NOT authorize "/a2" (token "a" != token "a2")
    {
        const json before = original;
        auto res = ed.apply(original, {ae::operation::set("/a2", "hacked")});
        CHECK(!res.success);
        CHECK(res.failed_index == 0);
        CHECK(res.reason.find("not covered") != std::string::npos);
        CHECK(original == before);
    }

    // second operation fails -> first operation must not be committed either
    {
        const json before = original;
        auto res = ed.apply(original, {
            ae::operation::set("/a/0/name", "changed"), // would succeed alone
            ae::operation::set("/a2", "hacked")         // unauthorized
        });
        CHECK(!res.success);
        CHECK(res.failed_index == 1);
        CHECK(original == before); // no partial modification leaked out
    }
}

static void test_tilde_escaping_single_token()
{
    std::cerr << "== '~1' decoding: /a~1b is one key, not two levels ==\n";

    const json original = {
        {"a/b", {{"x", 1}}}, // single key containing a slash
        {"a", {{"b", 2}}}    // two nested levels a -> b
    };

    // authorized prefix "/a~1b" covers only the single key "a/b"
    {
        ae::editor ed({"/a~1b"});
        const json before = original;

        auto ok = ed.apply(original, {ae::operation::set("/a~1b/x", 42)});
        CHECK(ok.success);
        CHECK(ok.document["a/b"]["x"] == 42);
        CHECK(ok.document["a"]["b"] == 2); // nested path untouched

        // the two-level path "/a/b" is not covered by prefix "/a~1b"
        auto denied = ed.apply(original, {ae::operation::set("/a/b", 99)});
        CHECK(!denied.success);
        CHECK(denied.failed_index == 0);
        CHECK(original == before);
    }

    // conversely, prefix "/a/b" covers the nested path but not "/a~1b"
    {
        ae::editor ed({"/a/b"});
        auto ok = ed.apply(original, {ae::operation::set("/a/b", 99)});
        CHECK(ok.success);
        CHECK(ok.document["a"]["b"] == 99);
        CHECK(ok.document["a/b"]["x"] == 1);

        auto denied = ed.apply(original, {ae::operation::set("/a~1b/x", 42)});
        CHECK(!denied.success);
    }
}

static void test_array_index_vs_object_key()
{
    std::cerr << "== array indices vs object string keys ==\n";
    ae::editor ed({"/a"});

    // token "0" on an object addresses the string key "0"
    {
        const json doc = {{"a", {{"0", "zero"}}}};
        auto res = ed.apply(doc, {ae::operation::set("/a/0", "Z")});
        CHECK(res.success);
        CHECK(res.document["a"].is_object());
        CHECK(res.document["a"]["0"] == "Z");
    }

    // token "0" on an array addresses element 0
    {
        const json doc = {{"a", json::array({1, 2, 3})}};
        auto res = ed.apply(doc, {ae::operation::set("/a/0", 9)});
        CHECK(res.success);
        CHECK(res.document["a"] == json::array({9, 2, 3}));
    }

    // non-numeric token on an array is rejected, not treated as a key
    {
        const json doc = {{"a", json::array({1, 2, 3})}};
        auto res = ed.apply(doc, {ae::operation::set("/a/name", 9)});
        CHECK(!res.success);
        CHECK(res.reason.find("not a valid array index") != std::string::npos);
    }

    // leading zeros are not valid array indices
    {
        const json doc = {{"a", json::array({1, 2, 3})}};
        auto res = ed.apply(doc, {ae::operation::set("/a/01", 9)});
        CHECK(!res.success);
        CHECK(res.reason.find("not a valid array index") != std::string::npos);
    }
}

static void test_array_remove_index_shift()
{
    std::cerr << "== array removal shifts subsequent indices ==\n";
    ae::editor ed({"/a"});
    const json original = {{"a", json::array({10, 20, 30, 40})}};

    // removing /a/0 shifts everything down; the following remove /a/1
    // therefore addresses the element that was at index 2 (value 30)
    {
        auto res = ed.apply(original, {
            ae::operation::remove("/a/0"), // -> [20, 30, 40]
            ae::operation::remove("/a/1")  // -> [20, 40]
        });
        CHECK(res.success);
        CHECK(res.document["a"] == json::array({20, 40}));
    }

    // remove then set observes the shifted indices as well
    {
        auto res = ed.apply(original, {
            ae::operation::remove("/a/1"),     // -> [10, 30, 40]
            ae::operation::set("/a/1", 99)     // -> [10, 99, 40]
        });
        CHECK(res.success);
        CHECK(res.document["a"] == json::array({10, 99, 40}));
    }

    // after the shift, a formerly valid index is out of range; the batch
    // fails at operation 1 and the first removal is NOT committed
    {
        const json before = original;
        auto res = ed.apply(original, {
            ae::operation::remove("/a/0"), // -> [20, 30, 40]
            ae::operation::remove("/a/3")  // index 3 no longer exists
        });
        CHECK(!res.success);
        CHECK(res.failed_index == 1);
        CHECK(res.reason.find("out of range") != std::string::npos);
        CHECK(original == before); // atomic: no partial modification
    }
}

static void test_subtree_remove_needs_parent_permission()
{
    std::cerr << "== removing a parent removes the whole subtree ==\n";

    const json original = {
        {"a", {{"b", {{"c", {{"deep", 1}}}}}, {"b2", 2}}},
        {"z", "keep"}
    };

    // authorized on "/a": removing "/a" deletes the entire subtree
    {
        ae::editor ed({"/a"});
        auto res = ed.apply(original, {ae::operation::remove("/a")});
        CHECK(res.success);
        CHECK(!res.document.contains("a"));
        CHECK(res.document["z"] == "keep");
    }

    // authorized only below "/a" ("/a/b"): removing the parent "/a" is denied
    {
        ae::editor ed({"/a/b"});
        const json before = original;
        auto res = ed.apply(original, {ae::operation::remove("/a")});
        CHECK(!res.success);
        CHECK(res.failed_index == 0);
        CHECK(res.reason.find("not covered") != std::string::npos);
        CHECK(original == before);
    }

    // authorized on "/a": removing a nested value inside the subtree works
    {
        ae::editor ed({"/a"});
        auto res = ed.apply(original, {ae::operation::remove("/a/b/c/deep")});
        CHECK(res.success);
        CHECK(res.document["a"]["b"]["c"].is_object());
        CHECK(res.document["a"]["b"]["c"].empty());
    }

    // removing a key that does not exist fails structurally
    {
        ae::editor ed({"/a"});
        auto res = ed.apply(original, {ae::operation::remove("/a/nope")});
        CHECK(!res.success);
        CHECK(res.reason.find("not found") != std::string::npos);
    }
}

static void test_root_and_misc()
{
    std::cerr << "== root pointer, append, malformed pointers ==\n";

    // the empty prefix authorizes the whole document, including the root
    {
        ae::editor ed({""});
        auto res = ed.apply(json{{"x", 1}}, {ae::operation::set("", json{{"y", 2}})});
        CHECK(res.success);
        CHECK(res.document == json({{"y", 2}}));
    }

    // a non-root prefix does not authorize replacing the root
    {
        ae::editor ed({"/a"});
        const json doc = {{"a", 1}};
        auto res = ed.apply(doc, {ae::operation::set("", json{{"b", 2}})});
        CHECK(!res.success);
        CHECK(doc == json({{"a", 1}}));
    }

    // removing the root is rejected with a clear reason
    {
        ae::editor ed({""});
        auto res = ed.apply(json{{"x", 1}}, {ae::operation::remove("")});
        CHECK(!res.success);
        CHECK(res.reason.find("root") != std::string::npos);
    }

    // "-" appends to an array; index == size appends as well
    {
        ae::editor ed({"/a"});
        const json doc = {{"a", json::array({1, 2})}};
        auto res = ed.apply(doc, {
            ae::operation::set("/a/-", 3),
            ae::operation::set("/a/3", 4) // size is 3 after the append
        });
        CHECK(res.success);
        CHECK(res.document["a"] == json::array({1, 2, 3, 4}));
    }

    // malformed pointers are reported with the operation index
    {
        ae::editor ed({"/a"});
        const json doc = {{"a", 1}};
        auto res = ed.apply(doc, {
            ae::operation::set("/a", 2),   // ok
            ae::operation::set("a", 3),    // missing leading '/'
        });
        CHECK(!res.success);
        CHECK(res.failed_index == 1);
        CHECK(res.reason.find("invalid JSON Pointer") != std::string::npos);
        CHECK(doc == json({{"a", 1}}));

        auto bad_escape = ed.apply(doc, {ae::operation::set("/a~2b", 1)});
        CHECK(!bad_escape.success);
        CHECK(bad_escape.reason.find("invalid JSON Pointer") != std::string::npos);
    }

    // invalid authorized prefixes are rejected at editor construction
    {
        bool threw = false;
        try
        {
            ae::editor ed({"a"}); // missing leading '/'
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        CHECK(threw);
    }

    // missing parent path is a structural failure
    {
        ae::editor ed({""}); // authorized everywhere, so structure decides
        const json doc = {{"a", 1}};
        auto res = ed.apply(doc, {ae::operation::set("/missing/x", 1)});
        CHECK(!res.success);
        CHECK(res.reason.find("parent path does not exist") != std::string::npos);
    }
}

int main()
{
    test_prefix_authorization_token_wise();
    test_tilde_escaping_single_token();
    test_array_index_vs_object_key();
    test_array_remove_index_shift();
    test_subtree_remove_needs_parent_permission();
    test_root_and_misc();

    std::cerr << (failures == 0 ? "ALL " : "FAILED: ")
              << (checks - failures) << '/' << checks << " checks passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
