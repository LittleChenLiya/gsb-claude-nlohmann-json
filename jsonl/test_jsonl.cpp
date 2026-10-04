// test_jsonl.cpp - focused acceptance tests for nlohmann::jsonl::decoder.
//
// Build (from the repository root):
//   g++ -std=c++17 -I single_include -I jsonl jsonl/test_jsonl.cpp -o jsonl/test_jsonl
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "jsonl_decoder.hpp"

using nlohmann::json;
using nlohmann::jsonl::decoder;
using nlohmann::jsonl::error;

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__            \
                      << ": " #cond << '\n';                               \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

// Feed a whole stream in fixed-size chunks, collecting all records.
static std::vector<json> collect(decoder& dec, const std::string& stream,
                                 std::size_t chunk)
{
    std::vector<json> all;
    for (std::size_t i = 0; i < stream.size(); i += chunk)
    {
        const std::size_t n = std::min(chunk, stream.size() - i);
        auto recs = dec.feed(stream.data() + i, n);
        all.insert(all.end(), recs.begin(), recs.end());
    }
    auto tail = dec.finish();
    all.insert(all.end(), tail.begin(), tail.end());
    return all;
}

// Acceptance: two records with Chinese text and escaped newlines, fed in
// 3-byte chunks (cutting multi-byte UTF-8 characters), decoded one by one.
static void test_acceptance_two_records_3byte_chunks()
{
    const std::string line1 = R"({"msg":"你好","text":"line1\nline2"})";
    const std::string line2 = R"({"msg":"世界","ok":true})";
    decoder dec;
    auto recs = collect(dec, line1 + "\n" + line2 + "\n", 3);
    CHECK(recs.size() == 2);
    CHECK(recs[0]["msg"] == "你好");
    CHECK(recs[0]["text"] == "line1\nline2");  // real newline inside the string
    CHECK(recs[1]["msg"] == "世界");
    CHECK(recs[1]["ok"] == true);
    CHECK(dec.records_delivered() == 2);
    CHECK(dec.bytes_consumed() == line1.size() + 1 + line2.size() + 1);
}

// The escaped "\n" inside a string must not split a record even when the
// backslash and 'n' arrive in different chunks.
static void test_escaped_newline_not_a_boundary()
{
    const std::string stream = R"({"a":"x\ny"})" "\n" R"({"b":2})" "\n";
    for (std::size_t chunk = 1; chunk <= 7; ++chunk)
    {
        decoder dec;
        auto recs = collect(dec, stream, chunk);
        CHECK(recs.size() == 2);
        CHECK(recs[0]["a"] == "x\ny");
        CHECK(recs[1]["b"] == 2);
    }
}

// Bad JSON in the second record: first record stays delivered, the error
// names record 2 and the global byte offset of the second record, and no
// later feed() can silently resume.
static void test_bad_second_record()
{
    const std::string good = R"({"msg":"你好"})";
    const std::string bad  = R"({"msg":"世界","ok":})";
    const std::string stream = good + "\n" + bad + "\n";
    decoder dec;
    std::vector<json> delivered;
    bool threw = false;
    try
    {
        // feed chunk by chunk, keeping every record as soon as it is delivered
        for (std::size_t i = 0; i < stream.size(); i += 3)
        {
            const std::size_t n = std::min<std::size_t>(3, stream.size() - i);
            auto recs = dec.feed(stream.data() + i, n);
            delivered.insert(delivered.end(), recs.begin(), recs.end());
        }
        auto tail = dec.finish();
        delivered.insert(delivered.end(), tail.begin(), tail.end());
    }
    catch (const error& e)
    {
        threw = true;
        CHECK(e.record() == 2);
        CHECK(e.byte_offset() == good.size() + 1);  // first byte of record 2
    }
    CHECK(threw);
    CHECK(dec.failed());
    CHECK(dec.records_delivered() == 1);
    // delivered record remains valid and usable
    CHECK(delivered.size() == 1);
    CHECK(delivered[0]["msg"] == "你好");
    // latched failure: feeding more data rethrows, nothing is decoded
    bool rethrew = false;
    try { dec.feed("{\"later\":1}\n"); }
    catch (const error& e)
    {
        rethrew = true;
        CHECK(e.record() == 2);
    }
    CHECK(rethrew);
    CHECK(dec.records_delivered() == 1);
}

// The per-record limit counts bytes, not characters: 中 is 3 bytes.
static void test_limit_counts_bytes_not_chars()
{
    const std::string rec = "{\"k\":\"中\"}";  // 11 bytes, 9 characters
    CHECK(rec.size() == 11);

    decoder ok(11);  // exactly the byte size -> accepted
    auto recs = collect(ok, rec + "\n", 2);
    CHECK(recs.size() == 1);
    CHECK(recs[0]["k"] == "中");

    decoder tight(9);  // the *character* count -> must fail (bytes counted)
    bool threw = false;
    try { collect(tight, rec + "\n", 2); }
    catch (const error& e)
    {
        threw = true;
        CHECK(e.record() == 1);
        CHECK(e.byte_offset() == 9);  // 10th byte (0-based 9) overflows
    }
    CHECK(threw);
}

// Over-limit mid-stream: error, latch, no resume.
static void test_over_limit_latches()
{
    decoder dec(5);
    bool threw = false;
    try { dec.feed("{\"abcdef\":1}\n"); }
    catch (const error& e)
    {
        threw = true;
        CHECK(e.record() == 1);
        CHECK(e.byte_offset() == 5);  // the byte that exceeded the limit
    }
    CHECK(threw);
    CHECK(dec.failed());
    bool rethrew = false;
    try { dec.feed("x"); } catch (const error&) { rethrew = true; }
    CHECK(rethrew);
}

// Blank-line strategy: empty and whitespace-only lines are skipped and do
// not count as records.
static void test_blank_lines_skipped()
{
    decoder dec;
    auto recs = collect(dec, "\n  \n\t\n{\"a\":1}\n\n \r\n{\"b\":2}\n\n", 4);
    CHECK(recs.size() == 2);
    CHECK(recs[0]["a"] == 1);
    CHECK(recs[1]["b"] == 2);
    CHECK(dec.records_delivered() == 2);
}

// CRLF line endings: the CR is stripped and never reaches the parser.
static void test_crlf()
{
    decoder dec;
    auto recs = collect(dec, "{\"a\":1}\r\n{\"b\":2}\r\n", 1);
    CHECK(recs.size() == 2);
    CHECK(recs[0]["a"] == 1);
    CHECK(recs[1]["b"] == 2);
}

// A final record without a trailing newline is flushed by finish().
static void test_final_record_without_newline()
{
    decoder dec;
    auto recs = collect(dec, "{\"a\":1}\n{\"b\":\"中\"}", 3);
    CHECK(recs.size() == 2);
    CHECK(recs[1]["b"] == "中");
}

// EOF in the middle of a multi-byte UTF-8 character is an error pointing at
// the incomplete lead byte; a complete trailing character is fine.
static void test_eof_partial_utf8()
{
    const std::string prefix = "{\"k\":\"";
    const std::string zhong = "中";  // E4 B8 AD
    decoder dec;
    dec.feed(prefix + zhong.substr(0, 2));  // lead + one continuation only
    bool threw = false;
    try { dec.finish(); }
    catch (const error& e)
    {
        threw = true;
        CHECK(e.record() == 1);
        CHECK(e.byte_offset() == prefix.size());  // offset of the 0xE4 lead byte
    }
    CHECK(threw);
    CHECK(dec.failed());

    decoder ok;
    auto recs = collect(ok, prefix + zhong + "\"}\n", 1);
    CHECK(recs.size() == 1);
    CHECK(recs[0]["k"] == "中");
}

// Multi-byte characters split across chunk boundaries (all chunk sizes).
static void test_chunked_utf8_all_splits()
{
    const std::string stream =
        "{\"a\":\"中文测试\"}\n{\"b\":\"𐍈\"}\n";  // 3-byte and 4-byte chars
    for (std::size_t chunk = 1; chunk <= stream.size(); ++chunk)
    {
        decoder dec;
        auto recs = collect(dec, stream, chunk);
        CHECK(recs.size() == 2);
        CHECK(recs[0]["a"] == "中文测试");
        CHECK(recs[1]["b"] == "𐍈");
    }
}

int main()
{
    test_acceptance_two_records_3byte_chunks();
    test_escaped_newline_not_a_boundary();
    test_bad_second_record();
    test_limit_counts_bytes_not_chars();
    test_over_limit_latches();
    test_blank_lines_skipped();
    test_crlf();
    test_final_record_without_newline();
    test_eof_partial_utf8();
    test_chunked_utf8_all_splits();
    if (failures == 0)
    {
        std::cout << "all jsonl decoder tests passed\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
