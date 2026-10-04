//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | |   | |  version 3.12.0
// |_____|_____|_|_|_|___|_|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

/*
Standalone acceptance tests for the streaming JSON diff tool
(include/nlohmann/json_diff.hpp). No test framework required.

Build and run (from the repository root):

    g++ -std=c++11 -Wall -Wextra -I include -o build/unit_stream_diff tests/stream_diff/unit_stream_diff.cpp
    ./build/unit_stream_diff
*/

#include <nlohmann/json_diff.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

namespace jd = nlohmann::json_diff;

static int failures = 0;

#define CHECK(cond)                                                        \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            ++failures;                                                    \
            std::cerr << "FAILED [" << __FILE__ << ':' << __LINE__         \
                      << "]: " << #cond << std::endl;                      \
        }                                                                  \
    } while (0)

static const char NL = static_cast<char>(0x0A);
static const char QUOTE = static_cast<char>(0x22);

// a streambuf that yields the input at most `chunk` bytes per refill,
// modelling input that arrives a few bytes at a time
class chunked_streambuf : public std::streambuf
{
  public:
    chunked_streambuf(const std::string& data, const std::size_t chunk)
        : m_data(data), m_chunk(chunk == 0 ? 1 : chunk)
    {}

  protected:
    int_type underflow() override
    {
        if (m_pos == m_data.size())
        {
            return traits_type::eof();
        }
        const std::size_t n = std::min(m_chunk, m_data.size() - m_pos);
        char* begin = const_cast<char*>(m_data.data()) + m_pos;
        setg(begin, begin, begin + n);
        m_pos += n;
        return traits_type::to_int_type(*gptr());
    }

  private:
    const std::string& m_data;
    std::size_t m_chunk;
    std::size_t m_pos = 0;
};

// compare two strings, both delivered through chunked streams
static jd::result compare_chunked(const std::string& a, const std::string& b,
                                  const std::size_t chunk, const jd::options& opt = jd::options())
{
    chunked_streambuf buf_a(a, chunk);
    chunked_streambuf buf_b(b, chunk);
    std::istream is_a(&buf_a);
    std::istream is_b(&buf_b);
    return jd::compare(is_a, is_b, opt);
}

static void test_equal_documents()
{
    std::cout << "test_equal_documents" << std::endl;
    CHECK(jd::compare("null", "null").outcome == jd::status::equal);
    CHECK(jd::compare("true", "true").outcome == jd::status::equal);
    CHECK(jd::compare("false", "false").outcome == jd::status::equal);
    CHECK(jd::compare("  42  ", "42").outcome == jd::status::equal);
    CHECK(jd::compare("\"hello\"", "\"hello\"").outcome == jd::status::equal);
    CHECK(jd::compare("{}", "{}").outcome == jd::status::equal);
    CHECK(jd::compare("[]", "[]").outcome == jd::status::equal);
    CHECK(jd::compare("[{},[[]],{\"a\":{}}]", "[{},[[]],{\"a\":{}}]").outcome == jd::status::equal);
    CHECK(jd::compare("{\"a\":1,\"b\":[1,2,{\"c\":null}]}", "{\"a\":1,\"b\":[1,2,{\"c\":null}]}").outcome == jd::status::equal);
}

static void test_deep_array_value_change()
{
    std::cout << "test_deep_array_value_change" << std::endl;

    // one value changed deep inside nested arrays
    const std::string a = "[[[[1,2,3],[4,5,6]]]]";
    const std::string b = "[[[[1,2,3],[4,7,6]]]]";
    const auto r = jd::compare(a, b);
    CHECK(r.outcome == jd::status::different);
    CHECK(r.kind == jd::diff_kind::value_mismatch);
    CHECK(r.json_pointer == "/0/0/1/1");
    CHECK(r.lhs.preview == "5");
    CHECK(r.rhs.preview == "7");

    // the reported position must correspond to the token in the original text:
    // offset is just past the token's last byte
    CHECK(r.lhs.position.offset == a.find('5') + 1);
    CHECK(r.rhs.position.offset == b.find('7') + 1);
    CHECK(a[r.lhs.position.offset - 1] == '5');
    CHECK(b[r.rhs.position.offset - 1] == '7');
    CHECK(r.lhs.position.line == 1);
    CHECK(r.lhs.position.column == a.find('5') + 1);

    // multi-line input: line/column must point at the changed token
    const std::string ml_a = "{\n  \"items\": [\n    1,\n    2,\n    3\n  ]\n}\n";
    const std::string ml_b = "{\n  \"items\": [\n    1,\n    2,\n    4\n  ]\n}\n";
    const auto r2 = jd::compare(ml_a, ml_b);
    CHECK(r2.outcome == jd::status::different);
    CHECK(r2.json_pointer == "/items/2");
    CHECK(r2.lhs.position.line == 5);
    CHECK(ml_b[r2.rhs.position.offset - 1] == '4');
    // column of the token "4" on its line
    const std::size_t line_start = ml_b.rfind(NL, r2.rhs.position.offset - 2) + 1;
    CHECK(r2.rhs.position.column == r2.rhs.position.offset - line_start);
}

static void test_member_order_change()
{
    std::cout << "test_member_order_change" << std::endl;

    // same members, different order: order is significant
    const std::string a = "{\"alpha\":1,\"beta\":2}";
    const std::string b = "{\"beta\":2,\"alpha\":1}";
    const auto r = jd::compare(a, b);
    CHECK(r.outcome == jd::status::different);
    CHECK(r.kind == jd::diff_kind::key_mismatch);
    CHECK(r.json_pointer == "/alpha");
    CHECK(r.lhs.preview == "key \"alpha\"");
    CHECK(r.rhs.preview == "key \"beta\"");
    // the key token position corresponds to the closing quote in the source
    CHECK(a[r.lhs.position.offset - 1] == QUOTE);
    CHECK(a.substr(2, r.lhs.position.offset - 3) == "alpha");

    // deeper: order change inside a nested object
    const auto r2 = jd::compare("{\"x\":[{\"p\":1,\"q\":2}]}", "{\"x\":[{\"q\":2,\"p\":1}]}");
    CHECK(r2.kind == jd::diff_kind::key_mismatch);
    CHECK(r2.json_pointer == "/x/0/p");

    // swapped members with identical values still differ
    const auto r3 = jd::compare("{\"a\":1,\"b\":1}", "{\"b\":1,\"a\":1}");
    CHECK(r3.kind == jd::diff_kind::key_mismatch);
}

static void test_equivalent_escaped_strings()
{
    std::cout << "test_equivalent_escaped_strings" << std::endl;

    // one JSON escape introducer (backslash) as a C++ string
    const std::string bs(1, static_cast<char>(0x5C));

    // strings are compared by decoded value
    CHECK(jd::compare("\"A\"", "\"" + bs + "u0041\"").outcome == jd::status::equal);
    CHECK(jd::compare("{\"k\":\"A\"}", "{\"k\":\"" + bs + "u0041\"}").outcome == jd::status::equal);
    CHECK(jd::compare("[\"a" + bs + "nb\"]", "[\"a" + bs + "u000Ab\"]").outcome == jd::status::equal);
    // surrogate pair vs. direct UTF-8 encoding of U+1D11E
    CHECK(jd::compare("\"" + bs + "uD834" + bs + "uDD1E\"", "\"\xF0\x9D\x84\x9E\"").outcome == jd::status::equal);
    // escaped keys, too
    CHECK(jd::compare("{\"" + bs + "u0041\":1}", "{\"A\":1}").outcome == jd::status::equal);

    // but genuinely different strings are reported
    const auto r = jd::compare("{\"k\":\"AB\"}", "{\"k\":\"" + bs + "u0041C\"}");
    CHECK(r.outcome == jd::status::different);
    CHECK(r.kind == jd::diff_kind::value_mismatch);
    CHECK(r.json_pointer == "/k");
}

static void test_syntax_error_priority()
{
    std::cout << "test_syntax_error_priority" << std::endl;

    // content differs early, but the second input is broken at the end:
    // the syntax error must win over the content difference
    const auto r1 = jd::compare("{\"x\":1,\"y\":2}", "{\"x\":999,\"y\":2,}");
    CHECK(r1.outcome == jd::status::syntax_error);
    CHECK(r1.error_side == 2);

    // same, broken first input
    const auto r2 = jd::compare("{\"x\":1,\"y\":2,}", "{\"x\":999,\"y\":2}");
    CHECK(r2.outcome == jd::status::syntax_error);
    CHECK(r2.error_side == 1);

    // truncated input after an early difference
    const auto r3 = jd::compare("[1,2,3]", "[0,2,3");
    CHECK(r3.outcome == jd::status::syntax_error);
    CHECK(r3.error_side == 2);

    // unterminated string after an early difference
    const auto r4 = jd::compare("[1,\"ok\"]", "[0,\"unterminated");
    CHECK(r4.outcome == jd::status::syntax_error);

    // trailing garbage is a syntax error
    const auto r5 = jd::compare("1", "1 x");
    CHECK(r5.outcome == jd::status::syntax_error);
    CHECK(r5.error_side == 2);

    // invalid number token
    const auto r6 = jd::compare("[1]", "[01]");
    CHECK(r6.outcome == jd::status::syntax_error);

    // invalid escape deep inside, after a difference
    const auto r7 = jd::compare("[0,\"a\"]", std::string("[1,\"") + static_cast<char>(0x5C) + "x\"]");
    CHECK(r7.outcome == jd::status::syntax_error);

    // empty input
    const auto r8 = jd::compare("", "1");
    CHECK(r8.outcome == jd::status::syntax_error);
    CHECK(r8.error_side == 1);

    // both inputs broken: still a syntax error, not a difference
    const auto r9 = jd::compare("[1,", "[2,");
    CHECK(r9.outcome == jd::status::syntax_error);

    // syntax error position points into the original text
    const std::string bad = "{\"a\":1,\"b\":}";
    const auto r10 = jd::compare("{\"a\":1,\"b\":0}", bad);
    CHECK(r10.outcome == jd::status::syntax_error);
    CHECK(r10.error_side == 2);
    CHECK(r10.rhs.position.offset <= bad.size());
    CHECK(bad[r10.rhs.position.offset - 1] == '}');
}

static void test_number_types()
{
    std::cout << "test_number_types" << std::endl;

    // library equality rules: 1 == 1.0 == 1e0 == 1E+0
    CHECK(jd::compare("1", "1.0").outcome == jd::status::equal);
    CHECK(jd::compare("1", "1e0").outcome == jd::status::equal);
    CHECK(jd::compare("[1]", "[1.0]").outcome == jd::status::equal);
    CHECK(jd::compare("10", "1e1").outcome == jd::status::equal);
    CHECK(jd::compare("-0", "-0.0").outcome == jd::status::equal);

    // different values of different number types: value mismatch, not type mismatch
    const auto r1 = jd::compare("1", "1.5");
    CHECK(r1.outcome == jd::status::different);
    CHECK(r1.kind == jd::diff_kind::value_mismatch);
    CHECK(r1.json_pointer.empty()); // root value
    CHECK(r1.lhs.preview == "1");
    CHECK(r1.rhs.preview == "1.5");

    const auto r2 = jd::compare("{\"n\":1}", "{\"n\":2}");
    CHECK(r2.kind == jd::diff_kind::value_mismatch);
    CHECK(r2.json_pointer == "/n");

    // integer vs. unsigned range handling (library rules)
    CHECK(jd::compare("18446744073709551615", "18446744073709551615").outcome == jd::status::equal);
    CHECK(jd::compare("-1", "18446744073709551615").outcome == jd::status::different);
    // 2^53+1 as integer vs. its (rounded) float spelling: not equal, exactly like the DOM
    CHECK(jd::compare("[9007199254740993]", "[9007199254740993.0]").outcome == jd::status::different);
    CHECK(jd::compare("[9007199254740992]", "[9007199254740992.0]").outcome == jd::status::equal);

    // number vs. string is a type mismatch
    const auto r3 = jd::compare("1", "\"1\"");
    CHECK(r3.kind == jd::diff_kind::type_mismatch);
}

static void test_empty_containers()
{
    std::cout << "test_empty_containers" << std::endl;

    CHECK(jd::compare("{}", "{}").outcome == jd::status::equal);
    CHECK(jd::compare("[]", "[]").outcome == jd::status::equal);
    CHECK(jd::compare("[[]]", "[[]]").outcome == jd::status::equal);

    const auto r1 = jd::compare("[]", "{}");
    CHECK(r1.kind == jd::diff_kind::type_mismatch);
    CHECK(r1.json_pointer.empty());

    const auto r2 = jd::compare("{}", "{\"a\":1}");
    CHECK(r2.kind == jd::diff_kind::object_size_mismatch);
    CHECK(r2.json_pointer == "/a");

    const auto r3 = jd::compare("{\"a\":1}", "{}");
    CHECK(r3.kind == jd::diff_kind::object_size_mismatch);
    CHECK(r3.json_pointer == "/a");

    const auto r4 = jd::compare("[]", "[1]");
    CHECK(r4.kind == jd::diff_kind::array_size_mismatch);
    CHECK(r4.json_pointer == "/0");

    const auto r5 = jd::compare("[1]", "[]");
    CHECK(r5.kind == jd::diff_kind::array_size_mismatch);
    CHECK(r5.json_pointer == "/0");

    // extra elements are located at the first index beyond the shorter array
    const auto r6 = jd::compare("[1,2]", "[1,2,3]");
    CHECK(r6.kind == jd::diff_kind::array_size_mismatch);
    CHECK(r6.json_pointer == "/2");

    // empty container vs. scalar
    const auto r7 = jd::compare("[]", "0");
    CHECK(r7.kind == jd::diff_kind::type_mismatch);
}

static void test_chunked_input()
{
    std::cout << "test_chunked_input" << std::endl;

    const std::string a = "{\"menu\":{\"items\":[1,2,{\"id\":\"x\"}],\"count\":3}}";
    const std::string b = "{\"menu\":{\"items\":[1,2,{\"id\":\"y\"}],\"count\":3}}";
    const std::string broken = "{\"menu\":{\"items\":[1,2,{\"id\":\"y\"}],\"count\":3,}}";

    for (std::size_t chunk = 1; chunk <= 7; ++chunk)
    {
        // equal inputs compare equal no matter how slowly they arrive
        CHECK(compare_chunked(a, a, chunk).outcome == jd::status::equal);

        // the first difference is found at the same position
        const auto r = compare_chunked(a, b, chunk);
        CHECK(r.outcome == jd::status::different);
        CHECK(r.kind == jd::diff_kind::value_mismatch);
        CHECK(r.json_pointer == "/menu/items/2/id");
        CHECK(r.lhs.preview == "\"x\"");
        CHECK(r.rhs.preview == "\"y\"");

        // a syntax error behind the difference is still detected
        const auto rb = compare_chunked(a, broken, chunk);
        CHECK(rb.outcome == jd::status::syntax_error);
        CHECK(rb.error_side == 2);
    }

    // mixed input kinds: string literal vs. chunked stream
    {
        chunked_streambuf buf(b, 2);
        std::istream is(&buf);
        const auto r = jd::compare(a, is);
        CHECK(r.kind == jd::diff_kind::value_mismatch);
    }
}

static void test_depth_limit()
{
    std::cout << "test_depth_limit" << std::endl;

    jd::options opt;
    opt.max_depth = 3;

    // within the limit: normal comparison
    CHECK(jd::compare("[[[1]]]", "[[[1]]]", opt).outcome == jd::status::equal);
    CHECK(jd::compare("[[[1]]]", "[[[2]]]", opt).outcome == jd::status::different);

    // exceeding the limit: explicit error
    const auto r1 = jd::compare("[[[[1]]]]", "[[[[1]]]]", opt);
    CHECK(r1.outcome == jd::status::depth_exceeded);
    CHECK(!r1.message.empty());

    // objects count towards the depth, too
    jd::options opt2;
    opt2.max_depth = 1;
    const auto r2 = jd::compare("{\"a\":{\"b\":1}}", "{\"a\":{\"b\":1}}", opt2);
    CHECK(r2.outcome == jd::status::depth_exceeded);

    // a syntax error beyond the limit still takes priority
    const auto r3 = jd::compare("[[[[1]]]]", "[[[[1]]]", opt);
    CHECK(r3.outcome == jd::status::syntax_error);

    // 0 means unlimited
    jd::options opt4;
    std::string deep = "1";
    for (int i = 0; i < 500; ++i)
    {
        deep = "[" + deep + "]";
    }
    CHECK(jd::compare(deep, deep, opt4).outcome == jd::status::equal);
}

static void test_truncated_report()
{
    std::cout << "test_truncated_report" << std::endl;

    const std::string long_a = "\"" + std::string(500, 'a') + "\"";
    const std::string long_b = "\"" + std::string(500, 'b') + "\"";

    jd::options opt;
    opt.max_preview_length = 16;
    const auto r = jd::compare(long_a, long_b, opt);
    CHECK(r.outcome == jd::status::different);
    CHECK(r.lhs.truncated);
    CHECK(r.rhs.truncated);
    CHECK(r.lhs.preview.size() == 16 + 3); // limit + "..."
    // the report never contains the whole value
    CHECK(r.message.size() < 200);

    // short values are not truncated
    const auto r2 = jd::compare("\"short\"", "\"shore\"", opt);
    CHECK(!r2.lhs.truncated);
    CHECK(r2.lhs.preview == "\"short\"");
}

static void test_pointer_escaping()
{
    std::cout << "test_pointer_escaping" << std::endl;

    // RFC 6901 escaping of ~ and / in keys
    const auto r = jd::compare("{\"a/b\":{\"~key\":1}}", "{\"a/b\":{\"~key\":2}}");
    CHECK(r.outcome == jd::status::different);
    CHECK(r.json_pointer == "/a~1b/~0key");
}

static void test_istream_and_iterator_inputs()
{
    std::cout << "test_istream_and_iterator_inputs" << std::endl;

    // std::istream inputs
    std::istringstream is1("[1,2,3]");
    std::istringstream is2("[1,2,4]");
    const auto r = jd::compare(is1, is2);
    CHECK(r.kind == jd::diff_kind::value_mismatch);
    CHECK(r.json_pointer == "/2");

    // contiguous containers
    const std::vector<char> v1 {'[', '1', ',', '2', ']'};
    const std::vector<char> v2 {'[', '1', ',', '2', ']'};
    CHECK(jd::compare(v1, v2).outcome == jd::status::equal);

    // std::string inputs
    const std::string s1 = "{\"a\":1}";
    const std::string s2 = "{\"a\":2}";
    CHECK(jd::compare(s1, s2).kind == jd::diff_kind::value_mismatch);
}

int main()
{
    test_equal_documents();
    test_deep_array_value_change();
    test_member_order_change();
    test_equivalent_escaped_strings();
    test_syntax_error_priority();
    test_number_types();
    test_empty_containers();
    test_chunked_input();
    test_depth_limit();
    test_truncated_report();
    test_pointer_escaping();
    test_istream_and_iterator_inputs();

    if (failures == 0)
    {
        std::cout << "ALL TESTS PASSED" << std::endl;
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " check(s) failed" << std::endl;
    return EXIT_FAILURE;
}
