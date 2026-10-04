// Deterministic tests for the JSON Patch preflight checker.
//
// Build (from the repository root):
//     g++ -std=c++11 -I include patch_preflight/test_preflight.cpp -o patch_preflight/test_preflight
//
// SPDX-License-Identifier: MIT

#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "json_patch_preflight.hpp"

namespace preflight = nlohmann::patch_preflight;
using nlohmann::json;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                        \
    do                                                                     \
    {                                                                      \
        ++checks;                                                          \
        if (!(cond))                                                       \
        {                                                                  \
            ++failures;                                                    \
            std::cerr << "CHECK failed (line " << __LINE__ << "): "        \
                      << #cond << '\n';                                    \
        }                                                                  \
    } while (0)

static bool message_contains(const preflight::op_report& op, const std::string& needle)
{
    return op.message.find(needle) != std::string::npos;
}

// Acceptance scenario: replace /a succeeds, test /b fails, the following
// remove is blocked, and the original document still holds the old "a".
static void test_replace_test_blocked()
{
    const json document = {{"a", "old"}, {"b", 1}, {"c", json::array({1, 2, 3})}};
    const json patch = json::array({
        {{"op", "replace"}, {"path", "/a"}, {"value", "new"}},
        {{"op", "test"}, {"path", "/b"}, {"value", 42}},
        {{"op", "remove"}, {"path", "/c"}}
    });

    const preflight::report rep = preflight::check(document, patch);

    CHECK(!rep.ok);
    CHECK(rep.error.empty());
    CHECK(rep.failed_index == 1);
    CHECK(rep.operations.size() == 3);

    CHECK(rep.operations[0].status == preflight::op_status::applied);
    CHECK(rep.operations[0].index == 0);
    CHECK(rep.operations[0].op == "replace");
    CHECK(rep.operations[0].path == "/a");

    CHECK(rep.operations[1].status == preflight::op_status::failed);
    CHECK(rep.operations[1].failed_at == preflight::pointer_role::path);
    CHECK(rep.operations[1].path == "/b");
    CHECK(message_contains(rep.operations[1], "/b"));

    CHECK(rep.operations[2].status == preflight::op_status::blocked);
    CHECK(rep.operations[2].op == "remove");
    CHECK(rep.operations[2].path == "/c");

    // the internal copy reflects only the applied prefix
    CHECK(rep.document["a"] == "new");
    CHECK(rep.document["b"] == 1);
    CHECK(rep.document.count("c") == 1);

    // the original document is untouched and still holds the old "a"
    CHECK(document["a"] == "old");
    CHECK(document.count("c") == 1);
}

// move with a missing "from" location must produce a "from" diagnostic.
static void test_move_missing_from()
{
    const json document = {{"x", 1}};
    const json patch = json::array({
        {{"op", "move"}, {"from", "/missing"}, {"path", "/y"}},
        {{"op", "add"}, {"path", "/z"}, {"value", 3}}
    });

    const preflight::report rep = preflight::check(document, patch);

    CHECK(!rep.ok);
    CHECK(rep.failed_index == 0);
    CHECK(rep.operations.size() == 2);

    const preflight::op_report& op0 = rep.operations[0];
    CHECK(op0.status == preflight::op_status::failed);
    CHECK(op0.failed_at == preflight::pointer_role::from);
    CHECK(op0.has_from);
    CHECK(op0.from == "/missing");
    CHECK(op0.path == "/y");
    CHECK(message_contains(op0, "missing"));

    CHECK(rep.operations[1].status == preflight::op_status::blocked);

    // document unchanged (nothing was applied)
    CHECK(rep.document == document);
    CHECK(document == json({{"x", 1}}));
}

// The empty pointer "" addresses the document root.
static void test_root_path()
{
    // add "" replaces the whole document
    {
        const json document = {{"a", 1}};
        const json patch = json::array({
            {{"op", "add"}, {"path", ""}, {"value", json::array({1, 2})}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(rep.ok);
        CHECK(rep.operations.size() == 1);
        CHECK(rep.operations[0].status == preflight::op_status::applied);
        CHECK(rep.operations[0].path.empty());
        CHECK(rep.document == json::array({1, 2}));
        CHECK(document == json({{"a", 1}}));
    }

    // replace "" replaces the root value
    {
        const json document = {{"a", 1}};
        const json patch = json::array({
            {{"op", "replace"}, {"path", ""}, {"value", 5}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(rep.ok);
        CHECK(rep.document == json(5));
    }

    // test "" compares against the root value
    {
        const json document = 5;
        const json patch = json::array({
            {{"op", "test"}, {"path", ""}, {"value", 5}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::applied);
    }

    // remove "" fails (the root has no parent), exactly like patch_inplace
    {
        const json document = {{"a", 1}};
        const json patch = json::array({
            {{"op", "remove"}, {"path", ""}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.failed_index == 0);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::path);
        CHECK(rep.document == document);
    }

    // copy from the root to a member
    {
        const json document = {{"a", 1}};
        const json patch = json::array({
            {{"op", "copy"}, {"from", ""}, {"path", "/b"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(rep.ok);
        CHECK(rep.document["b"] == document);
        CHECK(rep.document["a"] == 1);
    }
}

// JSON Pointer escaping: ~0 for '~', ~1 for '/'.
static void test_pointer_escaping()
{
    const json document = {
        {"a/b", {{"x", 1}}},
        {"m~n", 2},
        {"weird~/key", {{"k", "v"}}}
    };

    // escaped tokens in test/replace/move paths
    const json patch = json::array({
        {{"op", "test"}, {"path", "/a~1b/x"}, {"value", 1}},
        {{"op", "replace"}, {"path", "/m~0n"}, {"value", 3}},
        {{"op", "move"}, {"from", "/a~1b/x"}, {"path", "/weird~0~1key/y"}}
    });

    const preflight::report rep = preflight::check(document, patch);
    CHECK(rep.ok);
    CHECK(rep.operations.size() == 3);
    CHECK(rep.operations[0].status == preflight::op_status::applied);
    CHECK(rep.operations[1].status == preflight::op_status::applied);
    CHECK(rep.operations[2].status == preflight::op_status::applied);
    CHECK(rep.operations[2].from == "/a~1b/x");

    CHECK(rep.document["m~n"] == 3);
    CHECK(rep.document["weird~/key"]["y"] == 1);
    CHECK(rep.document["a/b"].is_object());
    CHECK(rep.document["a/b"].count("x") == 0);

    // the report keeps the raw (escaped) pointers
    CHECK(rep.operations[0].path == "/a~1b/x");
    CHECK(rep.operations[1].path == "/m~0n");

    // an invalid escape sequence makes the operation fail at "path"
    {
        const json bad_patch = json::array({
            {{"op", "test"}, {"path", "/a~2b"}, {"value", 1}}
        });
        const preflight::report bad_rep = preflight::check(document, bad_patch);
        CHECK(!bad_rep.ok);
        CHECK(bad_rep.operations[0].status == preflight::op_status::failed);
        CHECK(bad_rep.operations[0].failed_at == preflight::pointer_role::path);
        CHECK(bad_rep.operations[0].path == "/a~2b");
    }

    // an invalid escape in "from" is attributed to "from"
    {
        const json bad_patch = json::array({
            {{"op", "copy"}, {"from", "/m~9n"}, {"path", "/z"}}
        });
        const preflight::report bad_rep = preflight::check(document, bad_patch);
        CHECK(!bad_rep.ok);
        CHECK(bad_rep.operations[0].status == preflight::op_status::failed);
        CHECK(bad_rep.operations[0].failed_at == preflight::pointer_role::from);
    }

    // the input document was not modified
    CHECK(document["m~n"] == 2);
    CHECK(document["a/b"]["x"] == 1);
}

// The array index "-" is only valid as the last token of an "add" path.
static void test_array_dash()
{
    const json document = {{"arr", json::array({1, 2, 3})}};

    // add /arr/- appends: valid
    {
        const json patch = json::array({
            {{"op", "add"}, {"path", "/arr/-"}, {"value", 4}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(rep.ok);
        CHECK(rep.document["arr"] == json::array({1, 2, 3, 4}));
    }

    // "-" as the target of a copy (which is an add at "path"): valid
    {
        const json patch = json::array({
            {{"op", "copy"}, {"from", "/arr/0"}, {"path", "/arr/-"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(rep.ok);
        CHECK(rep.document["arr"] == json::array({1, 2, 3, 1}));
    }

    // remove /arr/- is invalid
    {
        const json patch = json::array({
            {{"op", "remove"}, {"path", "/arr/-"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::path);
    }

    // replace /arr/- is invalid
    {
        const json patch = json::array({
            {{"op", "replace"}, {"path", "/arr/-"}, {"value", 9}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::path);
    }

    // test /arr/- is invalid
    {
        const json patch = json::array({
            {{"op", "test"}, {"path", "/arr/-"}, {"value", 3}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::path);
    }

    // copy from /arr/- is invalid and attributed to "from"
    {
        const json patch = json::array({
            {{"op", "copy"}, {"from", "/arr/-"}, {"path", "/x"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::from);
    }

    // move from /arr/- is invalid and attributed to "from"
    {
        const json patch = json::array({
            {{"op", "move"}, {"from", "/arr/-"}, {"path", "/x"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::from);
    }

    // "-" in a non-final position of an add path is invalid
    {
        const json patch = json::array({
            {{"op", "add"}, {"path", "/arr/-/x"}, {"value", 1}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::path);
    }

    // the input document was not modified by any of the failing runs
    CHECK(document["arr"] == json::array({1, 2, 3}));
}

// Neither the input document nor the input patch may be modified.
static void test_input_immutability()
{
    const json document = {
        {"a", "old"},
        {"nested", {{"list", json::array({1, 2, 3})}, {"flag", true}}}
    };
    const json patch = json::array({
        {{"op", "replace"}, {"path", "/a"}, {"value", "new"}},
        {{"op", "add"}, {"path", "/nested/list/-"}, {"value", 4}},
        {{"op", "remove"}, {"path", "/nested/flag"}},
        {{"op", "move"}, {"from", "/a"}, {"path", "/b"}},
        {{"op", "copy"}, {"from", "/b"}, {"path", "/c"}},
        {{"op", "test"}, {"path", "/c"}, {"value", "new"}}
    });

    const std::string document_before = document.dump();
    const std::string patch_before = patch.dump();

    const preflight::report rep = preflight::check(document, patch);

    CHECK(rep.ok);
    CHECK(rep.operations.size() == 6);
    CHECK(document.dump() == document_before);
    CHECK(patch.dump() == patch_before);

    // a fully applicable patch yields the same document as json::patch()
    CHECK(rep.document == document.patch(patch));

    // immutability also holds when the patch fails midway
    const json failing_patch = json::array({
        {{"op", "replace"}, {"path", "/a"}, {"value", "new"}},
        {{"op", "remove"}, {"path", "/does/not/exist"}}
    });
    const preflight::report rep2 = preflight::check(document, failing_patch);
    CHECK(!rep2.ok);
    CHECK(document.dump() == document_before);
    CHECK(failing_patch.dump() == failing_patch.dump());
    CHECK(rep2.document["a"] == "new"); // applied prefix only
}

// Structural problems are reported per operation; later operations block.
static void test_structural_errors()
{
    const json document = {{"a", 1}};

    // the patch itself must be an array
    {
        const json patch = {{"op", "remove"}, {"path", "/a"}};
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(!rep.error.empty());
        CHECK(rep.operations.empty());
        CHECK(rep.document == document);
    }

    // entries must be objects
    {
        const json patch = json::array({
            {{"op", "remove"}, {"path", "/a"}},
            "not an object",
            {{"op", "remove"}, {"path", "/a"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.failed_index == 1);
        CHECK(rep.operations[0].status == preflight::op_status::applied);
        CHECK(rep.operations[1].status == preflight::op_status::failed);
        CHECK(rep.operations[1].failed_at == preflight::pointer_role::none);
        CHECK(rep.operations[2].status == preflight::op_status::blocked);
    }

    // invalid op value
    {
        const json patch = json::array({
            {{"op", "frobnicate"}, {"path", "/a"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::none);
        CHECK(message_contains(rep.operations[0], "frobnicate"));
    }

    // missing members
    {
        const json patch = json::array({
            {{"op", "add"}, {"path", "/b"}} // no "value"
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(message_contains(rep.operations[0], "value"));
    }
    {
        const json patch = json::array({
            {{"op", "move"}, {"path", "/b"}} // no "from"
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::from);
        CHECK(message_contains(rep.operations[0], "from"));
    }
    {
        const json patch = json::array({
            {{"path", "/a"}} // no "op"
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(message_contains(rep.operations[0], "op"));
    }
}

// Existence and test outcomes are genuinely evaluated, not syntax-checked.
static void test_genuine_execution()
{
    const json document = {{"a", 1}};

    // remove of a nonexistent key fails (existence is really checked)
    {
        const json patch = json::array({
            {{"op", "remove"}, {"path", "/nonexistent"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::path);
    }

    // replace of a nonexistent key fails
    {
        const json patch = json::array({
            {{"op", "replace"}, {"path", "/nonexistent"}, {"value", 1}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
    }

    // test on a nonexistent path fails with an existence diagnostic
    {
        const json patch = json::array({
            {{"op", "test"}, {"path", "/nonexistent"}, {"value", 1}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::path);
        CHECK(message_contains(rep.operations[0], "does not exist"));
    }

    // a passing test is really executed against the evolving copy:
    // op 1 can only succeed because op 0 ran on the copy
    {
        const json patch = json::array({
            {{"op", "replace"}, {"path", "/a"}, {"value", 2}},
            {{"op", "test"}, {"path", "/a"}, {"value", 2}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(rep.ok);
        CHECK(rep.document["a"] == 2);
        CHECK(document["a"] == 1);
    }

    // move into one's own child is rejected (RFC 6902 section 4.4)
    {
        const json patch = json::array({
            {{"op", "move"}, {"from", "/a"}, {"path", "/a/b"}}
        });
        const preflight::report rep = preflight::check(document, patch);
        CHECK(!rep.ok);
        CHECK(rep.operations[0].status == preflight::op_status::failed);
        CHECK(rep.operations[0].failed_at == preflight::pointer_role::from);
        CHECK(message_contains(rep.operations[0], "proper prefix"));
    }
}

int main()
{
    test_replace_test_blocked();
    test_move_missing_from();
    test_root_path();
    test_pointer_escaping();
    test_array_dash();
    test_input_immutability();
    test_structural_errors();
    test_genuine_execution();

    std::cout << checks << " checks, " << failures << " failures\n";
    if (failures == 0)
    {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << "TESTS FAILED\n";
    return 1;
}
