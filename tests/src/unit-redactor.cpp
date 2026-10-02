//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/json.hpp>
using nlohmann::json;
using nlohmann::json_redactor;
using nlohmann::redaction_rule;
#ifdef JSON_TEST_NO_GLOBAL_UDLS
    using namespace nlohmann::literals; // NOLINT(google-build-using-namespace)
#endif

#include <cstdio>
#include <fstream>
#include <sstream>
#include <streambuf>
#include <string>

namespace
{

std::string transcode_string(const json_redactor& redactor, const std::string& input)
{
    std::istringstream in(input);
    std::ostringstream out;
    redactor.transcode(in, out);
    return out.str();
}

// a streambuf that starts failing after a configurable number of characters
class failing_streambuf : public std::streambuf
{
  public:
    explicit failing_streambuf(std::streamsize fail_after)
        : remaining(fail_after)
    {}

  protected:
    int_type overflow(int_type ch) override
    {
        if (remaining <= 0)
        {
            return traits_type::eof();
        }
        --remaining;
        return ch;
    }

    std::streamsize xsputn(const char* /*s*/, std::streamsize count) override
    {
        if (remaining <= 0)
        {
            return 0;
        }
        if (count > remaining)
        {
            const std::streamsize written = remaining;
            remaining = 0;
            return written;
        }
        remaining -= count;
        return count;
    }

  private:
    std::streamsize remaining;
};

bool file_exists(const std::string& path)
{
    std::ifstream f(path.c_str());
    return f.good();
}

std::string read_file(const std::string& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    std::ostringstream content;
    content << f.rdbuf();
    return content.str();
}

void remove_file(const std::string& path)
{
    std::remove(path.c_str());
}

}  // namespace

TEST_CASE("streaming redaction transcoder")
{
    SECTION("acceptance: /users/1/token is replaced, everything else preserved")
    {
        const json_redactor redactor({{"/users/1/token", "\"***\""}});
        const std::string input =
            R"({"users":[{"name":"alice","token":"t0"},{"id":1,"token":"secret","keep":true}],"count":2})";

        const std::string output = transcode_string(redactor, input);

        // field order and all other values are preserved
        CHECK(output ==
              R"({"users":[{"name":"alice","token":"t0"},{"id":1,"token":"***","keep":true}],"count":2})");
        // output is a complete, valid JSON document, semantically as expected
        const json result = json::parse(output);
        CHECK(result["/users/1/token"_json_pointer] == "***");
        CHECK(result["/users/0/token"_json_pointer] == "t0");
        CHECK(result["/users/1/keep"_json_pointer] == true);
    }

    SECTION("corrupted JSON inside a redacted subtree still fails")
    {
        const json_redactor redactor({{"/users/1/token", "\"***\""}});
        // the syntax error hides inside the subtree that gets replaced
        const std::string input =
            R"({"users":[{"name":"alice"},{"token": {"broken": }}]})";

        std::istringstream in(input);
        std::ostringstream out;
        CHECK_THROWS_AS(redactor.transcode(in, out), json::parse_error&);
    }

    SECTION("empty pointer replaces the whole document")
    {
        const json_redactor redactor({{"", "{\"redacted\":true}"}});
        CHECK(transcode_string(redactor, R"({"a":[1,2,3],"b":"x"})") == "{\"redacted\":true}");
        CHECK(transcode_string(redactor, "[1,2,3]") == "{\"redacted\":true}");
        CHECK(transcode_string(redactor, "42") == "{\"redacted\":true}");
    }

    SECTION("tilde escapes in pointers")
    {
        const json_redactor redactor({{"/a~0b", "1"}, {"/a~1b", "2"}, {"/~01", "3"}});
        CHECK(transcode_string(redactor, R"({"a~b":0,"a/b":0,"~1":0,"ab":0})") ==
              R"({"a~b":1,"a/b":2,"~1":3,"ab":0})");
    }

    SECTION("nested rules: a matching parent suppresses child rules")
    {
        const json_redactor redactor({{"/a", "\"X\""}, {"/a/b", "\"Y\""}, {"/a/b/c", "\"Z\""}});
        CHECK(transcode_string(redactor, R"({"a":{"b":{"c":1},"d":2}})") == R"({"a":"X"})");

        // child rule alone still applies
        const json_redactor child_only({{"/a/b", "\"Y\""}});
        CHECK(transcode_string(child_only, R"({"a":{"b":1,"d":2}})") == R"({"a":{"b":"Y","d":2}})");
    }

    SECTION("array indices are counted on the original input")
    {
        // /arr/1 is a container that gets replaced; /arr/2 must still match
        // the element that was at index 2 in the original input
        const json_redactor redactor({{"/arr/1", "\"X\""}, {"/arr/2", "\"Y\""}});
        CHECK(transcode_string(redactor, R"({"arr":["a",{"b":1},"c","d"]})") ==
              R"({"arr":["a","X","Y","d"]})");

        // nested arrays
        const json_redactor nested({{"/0/1", "null"}, {"/1", "false"}});
        CHECK(transcode_string(nested, "[[0,1,2],[3,4]]") == "[[0,null,2],false]");
    }

    SECTION("redacted subtrees are validated but not reported")
    {
        // deeply nested replaced subtree; parser must still validate it
        const json_redactor redactor({{"/a", "null"}});
        CHECK(transcode_string(redactor, R"({"a":{"x":[1,{"y":[2,3]},4]},"b":1})") ==
              R"({"a":null,"b":1})");

        // trailing garbage after a valid document is an error
        std::istringstream in(R"({"a":1} garbage)");
        std::ostringstream out;
        CHECK_THROWS_AS(redactor.transcode(in, out), json::parse_error&);
    }

    SECTION("number precision of unmatched values is preserved")
    {
        const json_redactor redactor;
        const std::string input =
            R"({"float":1.0000000000000002,"exp":2.50e1,"small":1e-308,)"
            R"("umax":18446744073709551615,"imin":-9223372036854775808})";

        // floating-point tokens are re-emitted verbatim, integers exactly
        CHECK(transcode_string(redactor, input) == input);

        // and the values round-trip through the parser configuration
        CHECK(json::parse(transcode_string(redactor, input)) == json::parse(input));
    }

    SECTION("huge unmatched array is streamed")
    {
        // 1,000,000-element array that no rule matches, followed by a rule hit
        std::string input = "{\"big\":[";
        for (int i = 0; i < 1000000; ++i)
        {
            input += (i == 0) ? "0" : ",0";
        }
        input += "],\"token\":\"secret\"}";

        const json_redactor redactor({{"/token", "\"***\""}});
        const std::string output = transcode_string(redactor, input);

        const json result = json::parse(output);
        CHECK(result["big"].size() == 1000000);
        CHECK(result["token"] == "***");
        // output is a prefix of the input up to the replaced value
        CHECK(output.compare(0, input.size() - 8, input, 0, input.size() - 8) == 0);
    }

    SECTION("rules that do not match leave the document untouched")
    {
        const json_redactor redactor({{"/missing", "1"}, {"/a/5", "2"}});
        const std::string input = R"({"a":[1,2,3],"b":{"c":null}})";
        CHECK(transcode_string(redactor, input) == input);
    }

    SECTION("replacement values can be any JSON value")
    {
        const json_redactor redactor({{"/a", "[1,2,3]"}, {"/b", "{\"x\":null}"}, {"/c", "12.5"}});
        CHECK(transcode_string(redactor, R"({"a":1,"b":"text","c":{},"d":true})") ==
              R"({"a":[1,2,3],"b":{"x":null},"c":12.5,"d":true})");
    }

    SECTION("invalid rules are rejected")
    {
        // invalid JSON Pointer syntax
        CHECK_THROWS_AS(json_redactor({{"foo", "1"}}), json::parse_error&);
        CHECK_THROWS_AS(json_redactor({{"/~2", "1"}}), json::parse_error&);
        // replacement must be valid JSON
        CHECK_THROWS_AS(json_redactor({{"/a", "***"}}), json::other_error&);
        CHECK_THROWS_AS(json_redactor({{"/a", ""}}), json::other_error&);
        CHECK_THROWS_AS(json_redactor({{"/a", "{broken"}}), json::other_error&);
    }

    SECTION("sink write failure is reported")
    {
        const json_redactor redactor({{"/token", "\"***\""}});
        std::istringstream in(R"({"token":"secret","more":"data"})");
        failing_streambuf buf(5); // sink dies after 5 characters
        std::ostream out(&buf);
        CHECK_THROWS_AS(redactor.transcode(in, out), json::other_error&);
    }

    SECTION("transcode_file publishes only complete documents")
    {
        const std::string output_path = "test_unit_redactor_output.json";
        const json_redactor redactor({{"/users/1/token", "\"***\""}});

        SECTION("successful run publishes the redacted document")
        {
            remove_file(output_path);
            std::istringstream in(R"({"users":[{"token":"a"},{"token":"b"}]})");
            redactor.transcode_file(in, output_path);
            CHECK(read_file(output_path) == R"({"users":[{"token":"a"},{"token":"***"}]})");
            remove_file(output_path);
        }

        SECTION("corrupted input publishes nothing")
        {
            remove_file(output_path);
            std::istringstream in(R"({"users":[{"token":"a"},{"token": {"broken": }}]})");
            CHECK_THROWS_AS(redactor.transcode_file(in, output_path), json::parse_error&);
            CHECK_FALSE(file_exists(output_path));
        }

        SECTION("corrupted input leaves a pre-existing output file untouched")
        {
            {
                std::ofstream previous(output_path.c_str());
                previous << "{\"previous\":true}";
            }
            std::istringstream in(R"({"users":[{"token":"a"},{"token": }}})");
            CHECK_THROWS_AS(redactor.transcode_file(in, output_path), json::parse_error&);
            CHECK(read_file(output_path) == "{\"previous\":true}");
            remove_file(output_path);
        }

        SECTION("successful run replaces a pre-existing output file")
        {
            {
                std::ofstream previous(output_path.c_str());
                previous << "{\"previous\":true}";
            }
            std::istringstream in(R"({"users":[{"token":"a"},{"token":"b"}]})");
            redactor.transcode_file(in, output_path);
            CHECK(read_file(output_path) == R"({"users":[{"token":"a"},{"token":"***"}]})");
            remove_file(output_path);
        }

        SECTION("failing sink publishes nothing")
        {
            // staging path cannot be opened because the directory is missing
            std::istringstream in(R"({"users":[]})");
            CHECK_THROWS_AS(redactor.transcode_file(in, "no_such_dir/redactor_out.json"),
                            json::other_error&);
            CHECK_FALSE(file_exists("no_such_dir/redactor_out.json"));
        }
    }
}
