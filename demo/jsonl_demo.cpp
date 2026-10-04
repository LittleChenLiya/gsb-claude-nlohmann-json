//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

// Short chunked demo for the jsonl_stream_decoder.
//
// Feeds a JSONL stream (two records containing Chinese characters and an
// escaped newline) in 3-byte chunks and prints each parsed record.
//
// Build:
//   g++ -std=c++17 -I../include jsonl_demo.cpp -o jsonl_demo
//
// Run:
//   ./jsonl_demo

#include <nlohmann/jsonl_stream.hpp>

#include <iostream>
#include <string>

using nlohmann::json;
using nlohmann::jsonl_decoder_config;
using nlohmann::jsonl_stream_decoder;

int main()
{
    // Two JSONL records. The first contains an escaped newline (\n inside the
    // string) and the second contains Chinese characters. Both are fed in
    // 3-byte chunks to demonstrate chunked UTF-8 and escaped-newline handling.
    const std::string input =
        R"({"id":1,"msg":"hello\nworld"})" "\n"
        R"({"id":2,"msg":"你好"})" "\n";

    jsonl_decoder_config cfg;
    jsonl_stream_decoder decoder(cfg);

    std::cout << "Feeding " << input.size() << " bytes in 3-byte chunks...\n\n";

    for (std::size_t i = 0; i < input.size(); i += 3)
    {
        const std::size_t n = (std::min)(static_cast<std::size_t>(3), input.size() - i);
        const std::size_t delivered = decoder.feed(input.data() + i, n);

        std::cout << "chunk at offset " << i << " (" << n << " bytes): "
                  << "delivered " << delivered << " record(s)\n";
    }

    // Parse any final record that has no trailing newline (none here, but
    // finish() is required to close the stream).
    decoder.finish();

    std::cout << "\n=== Results ===\n";
    std::cout << "Total records: " << decoder.record_count() << "\n";
    std::cout << "Total bytes fed: " << decoder.bytes_fed() << "\n";

    if (decoder.errored())
    {
        std::cout << "ERROR at record " << decoder.error_record()
                  << " (byte offset " << decoder.error_byte_offset() << "): "
                  << decoder.error_message() << "\n";
        return 1;
    }

    for (std::size_t i = 0; i < decoder.record_count(); ++i)
    {
        const json& rec = decoder.records()[i];
        std::cout << "  record " << (i + 1) << ": " << rec.dump() << "\n";
    }

    std::cout << "\nAll records delivered successfully.\n";
    return 0;
}
