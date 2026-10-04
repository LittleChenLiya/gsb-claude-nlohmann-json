//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

// Streaming comparison demo.
//
// Reads two JSON documents from files and finds the first difference without
// building a DOM of either side. Both inputs are scanned token by token in
// lockstep; only the current nested path and the token at the first differing
// position are retained. After a difference is found, both remaining inputs
// are still fully read and checked for syntax errors, so a malformed tail is
// never reported as a content difference.
//
// Usage:
//   stream_compare_demo <file1> <file2> [max_nesting_depth]
//
// Example:
//   g++ -std=c++17 -I include tools/stream_compare_demo.cpp -o stream_compare_demo
//   ./stream_compare_demo a.json b.json
//
// Unit tests (from the repository root):
//   g++ -std=c++17 -I include -I tests/thirdparty/doctest tests/src/unit.cpp tests/src/unit-stream_compare.cpp -o unit-stream_compare
//   ./unit-stream_compare
//
// Or with CMake:
//   cmake -S . -B build -DJSON_BuildTests=ON
//   cmake --build build --target test-stream_compare_cpp17
//   ./build/tests/test-stream_compare_cpp17

#include <nlohmann/json_stream_compare.hpp>

#include <fstream>
#include <iostream>
#include <string>

using nlohmann::json;
using nlohmann::json_compare_options;
using nlohmann::json_diff;
using nlohmann::json_diff_kind;

namespace
{

const char* kind_name(json_diff_kind kind)
{
    switch (kind)
    {
        case json_diff_kind::equal:
            return "equal";
        case json_diff_kind::type_mismatch:
            return "type_mismatch";
        case json_diff_kind::value_mismatch:
            return "value_mismatch";
        case json_diff_kind::key_mismatch:
            return "key_mismatch";
        case json_diff_kind::array_length_mismatch:
            return "array_length_mismatch";
        case json_diff_kind::object_length_mismatch:
            return "object_length_mismatch";
        case json_diff_kind::nesting_too_deep:
            return "nesting_too_deep";
        case json_diff_kind::syntax_error:
            return "syntax_error";
    }
    return "unknown";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4)
    {
        std::cerr << "usage: " << argv[0] << " <file1> <file2> [max_nesting_depth]\n";
        return 2;
    }

    std::ifstream f1(argv[1], std::ios::binary);
    std::ifstream f2(argv[2], std::ios::binary);
    if (!f1 || !f2)
    {
        std::cerr << "error: cannot open input files\n";
        return 2;
    }

    json_compare_options opts;
    if (argc == 4)
    {
        opts.max_nesting_depth = static_cast<std::size_t>(std::stoull(argv[3]));
    }

    const json_diff diff = nlohmann::compare_json_stream(f1, f2, opts);

    json result;
    result["kind"] = kind_name(diff.kind);
    result["pointer"] = diff.pointer;
    result["left"] = json::object({
        {"value", diff.left_value},
        {"line", diff.left_line},
        {"column", diff.left_column},
        {"offset", diff.left_offset}
    });
    result["right"] = json::object({
        {"value", diff.right_value},
        {"line", diff.right_line},
        {"column", diff.right_column},
        {"offset", diff.right_offset}
    });
    if (!diff.error.empty())
    {
        result["error"] = diff.error;
    }

    std::cout << result.dump(2) << '\n';

    return diff.kind == json_diff_kind::equal ? 0 : 1;
}
