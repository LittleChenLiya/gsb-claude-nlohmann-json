// demo.cpp - short chunked-feeding demo of nlohmann::jsonl::decoder.
//
// Build (from the repository root):
//   g++ -std=c++17 -I single_include -I jsonl jsonl/demo.cpp -o jsonl/demo
//
// Scenario 1: two JSONL records containing Chinese text and an escaped
// newline (\n inside a JSON string), fed 3 bytes at a time - chunk
// boundaries deliberately cut multi-byte UTF-8 characters.
// Scenario 2: the second record is broken JSON; the first record stays
// valid and the error reports record number and global byte position.
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "jsonl_decoder.hpp"

static std::vector<nlohmann::json> run_stream(const std::string& stream,
                                              std::size_t chunk_size)
{
    nlohmann::jsonl::decoder dec;
    std::vector<nlohmann::json> all;
    for (std::size_t i = 0; i < stream.size(); i += chunk_size)
    {
        const std::size_t n = std::min(chunk_size, stream.size() - i);
        auto records = dec.feed(stream.data() + i, n);
        for (auto& r : records)
        {
            std::cout << "  record " << dec.records_delivered()
                      << " delivered after byte " << dec.bytes_consumed()
                      << ": " << r.dump(-1, ' ', true) << '\n';
            all.push_back(std::move(r));
        }
    }
    for (auto& r : dec.finish())  // final record without trailing LF
    {
        std::cout << "  record " << dec.records_delivered()
                  << " delivered at finish(): " << r.dump(-1, ' ', true) << '\n';
        all.push_back(std::move(r));
    }
    return all;
}

int main()
{
    const std::string line1 = R"({"msg":"你好","text":"line1\nline2"})";
    const std::string line2 = R"({"msg":"世界","ok":true})";

    std::cout << "scenario 1: two good records, 3-byte chunks\n";
    auto good = run_stream(line1 + "\n" + line2 + "\n", 3);
    std::cout << "  -> " << good.size() << " records, text of #1: \""
              << good[0]["text"].get<std::string>() << "\"\n\n";

    std::cout << "scenario 2: second record is broken JSON\n";
    const std::string bad = R"({"msg":"世界","ok":})";  // missing value
    try
    {
        run_stream(line1 + "\n" + bad + "\n", 3);
        std::cout << "  unexpected: no error\n";
        return 1;
    }
    catch (const nlohmann::jsonl::error& e)
    {
        std::cout << "  error at record " << e.record()
                  << ", global byte " << e.byte_offset() << ":\n    "
                  << e.what() << '\n';
        // The first record was delivered before the failure and stays valid.
        std::cout << "  first record still valid: "
                  << (good[0]["msg"] == "你好" ? "yes" : "no") << '\n';
    }
    return 0;
}
