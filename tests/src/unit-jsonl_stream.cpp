//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/jsonl_stream.hpp>
using nlohmann::json;
using nlohmann::jsonl_decoder_config;
using nlohmann::jsonl_stream_decoder;

#include <string>
#include <algorithm>

namespace
{
// Feed @a data to @a dec in chunks of @a chunk_size bytes.
void feed_chunks(jsonl_stream_decoder& dec, const std::string& data, std::size_t chunk_size)
{
    for (std::size_t i = 0; i < data.size(); i += chunk_size)
    {
        const std::size_t n = (std::min)(chunk_size, data.size() - i);
        dec.feed(data.data() + i, n);
    }
}
} // namespace

TEST_CASE("jsonl_stream_decoder")
{
    SECTION("two records with Chinese and escaped newline, fed in 3-byte chunks")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":"hello\nworld"})" "\n"
            R"({"b":"你好"})" "\n";

        feed_chunks(dec, data, 3);
        dec.finish();

        CHECK(dec.record_count() == 2);
        CHECK(dec.records()[0]["a"] == "hello\nworld");
        CHECK(dec.records()[1]["b"] == "你好");
        CHECK_FALSE(dec.errored());
    }

    SECTION("second record bad JSON: first retained, error points to second")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":1})" "\n"
            R"({"b":bad})" "\n";

        feed_chunks(dec, data, 3);
        dec.finish();

        // first record is retained
        CHECK(dec.record_count() == 1);
        CHECK(dec.records()[0]["a"] == 1);

        // error points to the second record
        CHECK(dec.errored());
        CHECK(dec.error_record() == 2);
        // first record is 8 bytes: {"a":1} then \n -> second starts at offset 8
        CHECK(dec.error_byte_offset() == 8);
        CHECK_FALSE(dec.error_message().empty());
    }

    SECTION("byte limit is in bytes, not characters")
    {
        jsonl_decoder_config cfg;
        cfg.max_record_bytes = 10; // 10 bytes max
        jsonl_stream_decoder dec(cfg);

        // {"a":"你好"} = 7 ASCII bytes + 6 UTF-8 bytes = 13 bytes
        const std::string data = R"({"a":"你好"})" "\n";

        dec.feed(data);
        dec.finish();

        CHECK(dec.errored());
        CHECK(dec.error_record() == 1);
    }

    SECTION("blank lines are skipped by default")
    {
        jsonl_decoder_config cfg;
        cfg.skip_blank_lines = true;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":1})" "\n"
            "\n"
            "   " "\n"
            R"({"b":2})" "\n";

        dec.feed(data);
        dec.finish();

        CHECK(dec.record_count() == 2);
        CHECK_FALSE(dec.errored());
    }

    SECTION("blank lines parsed as errors when skip_blank_lines is false")
    {
        jsonl_decoder_config cfg;
        cfg.skip_blank_lines = false;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":1})" "\n"
            "\n";

        dec.feed(data);
        dec.finish();

        CHECK(dec.errored());
        CHECK(dec.error_record() == 2);
    }

    SECTION("EOF half-character: incomplete UTF-8 at EOF is rejected")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data = R"({"a":1})" "\n";
        // "你" is E4 BD A0; feed only E4 BD (2 bytes) at EOF -> incomplete UTF-8
        const std::string data2 = R"({"c":")" "\xE4\xBD";

        dec.feed(data);
        dec.feed(data2);
        dec.finish();

        // first record is retained
        CHECK(dec.record_count() == 1);
        CHECK(dec.errored());
        CHECK(dec.error_record() == 2);
    }

    SECTION("over-limit: record exceeding max bytes is rejected")
    {
        jsonl_decoder_config cfg;
        cfg.max_record_bytes = 5;
        jsonl_stream_decoder dec(cfg);

        const std::string data = R"({"a":12345})" "\n";

        dec.feed(data);
        dec.finish();

        CHECK(dec.errored());
        CHECK(dec.error_record() == 1);
    }

    SECTION("CRLF line endings are handled")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":1})" "\r\n"
            R"({"b":2})" "\r\n";

        dec.feed(data);
        dec.finish();

        CHECK(dec.record_count() == 2);
        CHECK_FALSE(dec.errored());
    }

    SECTION("CRLF split across chunks is handled")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":1})" "\r\n"
            R"({"b":2})" "\r\n";

        feed_chunks(dec, data, 3);
        dec.finish();

        CHECK(dec.record_count() == 2);
        CHECK_FALSE(dec.errored());
    }

    SECTION("lone CR line endings are handled")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":1})" "\r"
            R"({"b":2})" "\r";

        dec.feed(data);
        dec.finish();

        CHECK(dec.record_count() == 2);
        CHECK_FALSE(dec.errored());
    }

    SECTION("last record without trailing newline is parsed")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":1})" "\n"
            R"({"b":2})"; // no trailing newline

        dec.feed(data);
        dec.finish();

        CHECK(dec.record_count() == 2);
        CHECK_FALSE(dec.errored());
    }

    SECTION("subsequent feeds after an error do not recover")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data =
            R"({"a":1})" "\n"
            R"({"b":bad})" "\n"
            R"({"c":3})" "\n";

        dec.feed(data);
        dec.finish();

        CHECK(dec.errored());
        CHECK(dec.record_count() == 1);

        // try to feed more data after the error
        const std::string more = R"({"d":4})" "\n";
        const std::size_t delivered = dec.feed(more);

        CHECK(delivered == 0);
        CHECK(dec.record_count() == 1);
    }

    SECTION("escaped newline in string is not a record boundary")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        // the string contains \n (escape: backslash + n), not a physical newline
        const std::string data = R"({"a":"line1\nline2"})" "\n";

        dec.feed(data);
        dec.finish();

        CHECK(dec.record_count() == 1);
        CHECK(dec.records()[0]["a"] == "line1\nline2");
        CHECK_FALSE(dec.errored());
    }

    SECTION("bytes_fed tracks total bytes across feeds")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data = R"({"a":1})" "\n" R"({"b":2})" "\n";

        dec.feed(data.data(), 5);
        CHECK(dec.bytes_fed() == 5);

        dec.feed(data.data() + 5, data.size() - 5);
        CHECK(dec.bytes_fed() == data.size());

        dec.finish();
        CHECK(dec.bytes_fed() == data.size());
    }

    SECTION("finish after error has no effect")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        const std::string data = R"({"b":bad})" "\n";

        dec.feed(data);
        const std::size_t first = dec.finish();
        const std::size_t second = dec.finish();

        CHECK(first == 0);
        CHECK(second == 0);
        CHECK(dec.errored());
    }

    SECTION("empty input produces no records")
    {
        jsonl_decoder_config cfg;
        jsonl_stream_decoder dec(cfg);

        dec.finish();

        CHECK(dec.record_count() == 0);
        CHECK_FALSE(dec.errored());
    }
}
