//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/redact.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using nlohmann::json;
using nlohmann::json_redactor;
using nlohmann::redaction_file_sink;
using nlohmann::redaction_rule;
using nlohmann::redaction_sink;

namespace
{

// test-only sink staging into memory; the library's redaction_file_sink
// provides the file-based staging used in production
class string_sink final : public redaction_sink
{
  public:
    void write(const char* data, std::size_t size) override
    {
        staged.append(data, size);
    }

    void commit() override
    {
        published = staged;
        ++commits;
    }

    void abort() noexcept override
    {
        staged.clear();
        ++aborts;
    }

    std::string staged;
    std::string published;
    int commits = 0;
    int aborts = 0;
};

// sink that fails to write after a configurable number of bytes
class failing_sink final : public redaction_sink
{
  public:
    explicit failing_sink(std::size_t fail_after)
        : fail_after_(fail_after)
    {}

    void write(const char* data, std::size_t size) override
    {
        written_ += size;
        if (written_ > fail_after_)
        {
            throw std::runtime_error("simulated sink write failure");
        }
        staged_.append(data, size);
    }

    void commit() override
    {
        ++commits_;
    }

    void abort() noexcept override
    {
        ++aborts_;
    }

    std::size_t fail_after_;
    std::size_t written_ = 0;
    std::string staged_;
    int commits_ = 0;
    int aborts_ = 0;
};

std::string read_file(const std::string& path)
{
    std::ifstream stream(path.c_str(), std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool file_exists(const std::string& path)
{
    return static_cast<bool>(std::ifstream(path.c_str()));
}

void write_file(const std::string& path, const std::string& content)
{
    std::ofstream stream(path.c_str(), std::ios::binary | std::ios::trunc);
    stream << content;
}

} // namespace

TEST_CASE("streaming redaction transcoder")
{
    SECTION("acceptance: /users/1/token is replaced, everything else preserved")
    {
        const std::string input =
            R"({"users":[{"name":"alice","token":"t-0"},{"name":"bob","token":{"secret":[1,2,3],"rotates":true}}],"count":2,"pi":3.141592653589793238462643383279,"nothing":null})";

        string_sink sink;
        json_redactor redactor({{"/users/1/token", "***"}}, sink);
        CHECK(redactor.transcribe(input));

        const std::string expected =
            R"({"users":[{"name":"alice","token":"t-0"},{"name":"bob","token":"***"}],"count":2,"pi":3.141592653589793238462643383279,"nothing":null})";
        CHECK(sink.published == expected);
        CHECK(sink.commits == 1);
        CHECK(sink.aborts == 0);

        // the output is complete, valid JSON and semantically equal
        CHECK(json::parse(sink.published) == json::parse(expected));
    }

    SECTION("corrupt JSON hidden inside a replaced subtree still fails")
    {
        // the syntax error sits inside the subtree replaced by the rule
        const std::string input =
            R"({"users":[{"name":"alice"},{"token":{"secret":[1,2,],"oops":}}],"count":2})";

        string_sink sink;
        json_redactor redactor({{"/users/1/token", "***"}}, sink);
        CHECK_FALSE(redactor.transcribe(input));
        CHECK(sink.commits == 0);
        CHECK(sink.aborts == 1);
        CHECK(sink.published.empty());
    }

    SECTION("empty pointer replaces the whole document")
    {
        string_sink sink;
        json_redactor redactor({{"", {{"redacted", true}}}}, sink);
        CHECK(redactor.transcribe(R"({"a":[1,2,3],"b":"x"})"));
        CHECK(sink.published == R"({"redacted":true})");
    }

    SECTION("~0 and ~1 escapes in pointers")
    {
        const std::string input = R"({"a~b":{"c/d":"secret","other":1},"tilde~":"x"})";

        string_sink sink;
        json_redactor redactor({{"/a~0b/c~1d", "***"}, {"/tilde~0", "hidden"}}, sink);
        CHECK(redactor.transcribe(input));
        CHECK(sink.published == R"({"a~b":{"c/d":"***","other":1},"tilde~":"hidden"})");
    }

    SECTION("nested rules: parent match disables child rules")
    {
        const std::string input = R"({"a":{"b":"secret","c":"keep-inside"},"d":1})";

        SECTION("parent wins over child")
        {
            string_sink sink;
            json_redactor redactor({{"/a", "REDACTED"}, {"/a/b", "CHILD"}}, sink);
            CHECK(redactor.transcribe(input));
            CHECK(sink.published == R"({"a":"REDACTED","d":1})");
        }

        SECTION("child rule applies when parent does not match")
        {
            string_sink sink;
            json_redactor redactor({{"/a/b", "CHILD"}}, sink);
            CHECK(redactor.transcribe(input));
            CHECK(sink.published == R"({"a":{"b":"CHILD","c":"keep-inside"},"d":1})");
        }
    }

    SECTION("array indices refer to the original input")
    {
        const std::string input = R"({"arr":["zero","one","two","three"]})";

        string_sink sink;
        json_redactor redactor({{"/arr/1", "X"}, {"/arr/3", "Y"}}, sink);
        CHECK(redactor.transcribe(input));
        CHECK(sink.published == R"({"arr":["zero","X","two","Y"]})");
    }

    SECTION("replaced container inside an array keeps following indices")
    {
        const std::string input = R"(["a",{"drop":[1,2,3]},"c","d"])";

        string_sink sink;
        json_redactor redactor({{"/1", "X"}, {"/3", "Y"}}, sink);
        CHECK(redactor.transcribe(input));
        CHECK(sink.published == R"(["a","X","c","Y"])");
    }

    SECTION("unmatched numbers keep their precision")
    {
        const std::string input =
            R"({"f":3.141592653589793238462643383279,"e":1e2,"neg":-0.5,"i":-9223372036854775807,"u":18446744073709551615})";

        string_sink sink;
        json_redactor redactor({{"/missing", "X"}}, sink);
        CHECK(redactor.transcribe(input));
        // floating-point tokens are echoed verbatim
        CHECK(sink.published == input);
        CHECK(json::parse(sink.published) == json::parse(input));
    }

    SECTION("huge unmatched array streams without building a DOM")
    {
        // 1,000,000-element array; one element near the end is redacted by
        // its original index
        const std::size_t count = 1000000;
        std::string input = "[";
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i > 0)
            {
                input += ',';
            }
            input += std::to_string(i);
        }
        input += ']';

        string_sink sink;
        json_redactor redactor({{"/999998", "redacted"}}, sink);
        CHECK(redactor.transcribe(input));

        // verify head, redacted element, and tail without storing a
        // second copy of the expected document
        CHECK(sink.published.compare(0, 7, "[0,1,2,") == 0);
        CHECK(sink.published.find("\"redacted\"") != std::string::npos);
        CHECK(sink.published.find("999998") == std::string::npos);
        CHECK(sink.published.size() > input.size() - 16);

        json const reparsed = json::parse(sink.published);
        CHECK(reparsed.size() == count);
        CHECK(reparsed[999998] == "redacted");
        CHECK(reparsed[999997] == 999997);
        CHECK(reparsed[999999] == 999999);
    }

    SECTION("sink write failure propagates and publishes nothing")
    {
        const std::string input = R"({"a":[1,2,3],"b":"some longer string to write"})";

        failing_sink sink(8); // fail after a few bytes
        json_redactor redactor({{"/b", "X"}}, sink);
        CHECK_THROWS_AS(redactor.transcribe(input), std::runtime_error);
        CHECK(sink.commits_ == 0);
        CHECK(sink.aborts_ == 1);
    }

    SECTION("file sink: publish boundary before and after commit")
    {
        const std::string target = "unit_redact_file_sink_test.json";
        const std::string input =
            R"({"users":[{"name":"alice"},{"name":"bob","token":"secret"}]})";

        SECTION("successful transcribe replaces the target atomically")
        {
            write_file(target, "SENTINEL-OLD-CONTENT");
            {
                redaction_file_sink sink(target);
                CHECK(file_exists(sink.temp_path()));
                CHECK(read_file(target) == "SENTINEL-OLD-CONTENT"); // not yet published

                json_redactor redactor({{"/users/1/token", "***"}}, sink);
                CHECK(redactor.transcribe(input));

                CHECK(read_file(target) ==
                      R"({"users":[{"name":"alice"},{"name":"bob","token":"***"}]})");
                CHECK_FALSE(file_exists(sink.temp_path())); // staging file consumed
            }
            std::remove(target.c_str());
        }

        SECTION("corrupt input leaves the previous target untouched")
        {
            write_file(target, "SENTINEL-OLD-CONTENT");
            {
                redaction_file_sink sink(target);
                json_redactor redactor({{"/users/1/token", "***"}}, sink);
                const std::string bad =
                    R"({"users":[{"name":"alice"},{"name":"bob","token":{"bad":]}}]})";
                CHECK_FALSE(redactor.transcribe(bad));

                CHECK(read_file(target) == "SENTINEL-OLD-CONTENT"); // zero publish
                CHECK_FALSE(file_exists(sink.temp_path()));         // staging file removed
            }
            std::remove(target.c_str());
        }

        SECTION("destructor without commit discards the staging file")
        {
            std::string temp;
            {
                redaction_file_sink sink(target);
                temp = sink.temp_path();
                sink.write("partial", 7);
                CHECK(file_exists(temp));
            }
            CHECK_FALSE(file_exists(temp));
        }
    }

    SECTION("no rules echo the input token stream")
    {
        const std::string input =
            R"({"obj":{"nested":[1,2.5,"three",null,true,false,{}]},"empty":[]})";

        string_sink sink;
        json_redactor redactor({}, sink);
        CHECK(redactor.transcribe(input));
        CHECK(sink.published == input);
    }

    SECTION("later duplicate rules override earlier ones")
    {
        string_sink sink;
        json_redactor redactor({{"/a", "first"}, {"/a", "second"}}, sink);
        CHECK(redactor.transcribe(R"({"a":1})"));
        CHECK(sink.published == R"({"a":"second"})");
    }
}
