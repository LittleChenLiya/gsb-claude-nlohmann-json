//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

// Streaming redaction transcoder demo.
//
// Demonstrates, in a few seconds:
//   1. normal redaction of /users/1/token with atomic publish
//   2. corrupted input (syntax error hidden inside the replaced subtree)
//      resulting in zero publication
//   3. streaming of a large unmatched array without building a DOM

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/redact.hpp>

namespace
{
std::string read_file(const std::string& path)
{
    std::ifstream stream(path.c_str(), std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void write_file(const std::string& path, const std::string& content)
{
    std::ofstream stream(path.c_str(), std::ios::binary | std::ios::trunc);
    stream << content;
}
} // namespace

int main()
{
    const auto started = std::chrono::steady_clock::now();

    // --- 1. normal redaction ---------------------------------------------
    std::cout << "[1] redact /users/1/token\n";
    const std::string input =
        R"({"users":[{"name":"alice","token":"t0"},{"name":"bob","token":{"secret":[1,2,3]}}],"version":3})";
    std::cout << "    input : " << input << '\n';
    {
        nlohmann::redaction_file_sink sink("demo_redacted.json");
        nlohmann::json_redactor redactor({{"/users/1/token", "***"}}, sink);
        if (!redactor.transcribe(input))
        {
            std::cerr << "    unexpected parse failure\n";
            return 1;
        }
    }
    std::cout << "    output: " << read_file("demo_redacted.json") << '\n';

    // --- 2. corrupted input: zero publication -----------------------------
    std::cout << "[2] corrupted input (error hidden inside the replaced subtree)\n";
    write_file("demo_corrupt.json", "PREVIOUSLY-PUBLISHED-DOCUMENT");
    {
        nlohmann::redaction_file_sink sink("demo_corrupt.json");
        nlohmann::json_redactor redactor({{"/users/1/token", "***"}}, sink);
        const std::string broken =
            R"({"users":[{"name":"alice"},{"token":{"secret":]}}]})";
        const bool published = redactor.transcribe(broken);
        std::cout << "    transcribe returned: " << std::boolalpha << published << '\n';
    }
    std::cout << "    target after failure: " << read_file("demo_corrupt.json") << '\n';

    // --- 3. large unmatched array ------------------------------------------
    std::cout << "[3] stream a 5,000,000-element array (no DOM is built)\n";
    {
        nlohmann::redaction_file_sink sink("demo_large.json");
        nlohmann::json_redactor redactor({{"/4999999", "redacted"}}, sink);
        // the output is what must not be held in memory, and it is staged
        // on disk by redaction_file_sink
        std::string big = "[";
        for (std::size_t i = 0; i < 5000000; ++i)
        {
            if (i > 0)
            {
                big += ',';
            }
            big += std::to_string(i);
        }
        big += ']';
        std::istringstream input_stream(big);
        if (!redactor.transcribe(input_stream))
        {
            std::cerr << "    unexpected parse failure\n";
            return 1;
        }
    }
    {
        std::ifstream in("demo_large.json", std::ios::binary | std::ios::ate);
        std::cout << "    published bytes: " << in.tellg() << '\n';
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started).count();
    std::cout << "done in " << elapsed << " ms\n";

    std::remove("demo_redacted.json");
    std::remove("demo_corrupt.json");
    std::remove("demo_large.json");
    return 0;
}
