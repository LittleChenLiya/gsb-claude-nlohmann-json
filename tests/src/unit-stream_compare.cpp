//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/json_stream_compare.hpp>

#include <sstream>
#include <string>
#include <vector>

using nlohmann::json;
using nlohmann::json_compare_options;
using nlohmann::json_diff;
using nlohmann::json_diff_kind;

namespace
{

class chunked_input_adapter
{
  public:
    using char_type = char;

    chunked_input_adapter(const std::string& s, std::size_t chunk)
        : data_(s), chunk_(chunk)
    {}

    std::char_traits<char>::int_type get_character()
    {
        if (buf_.empty())
        {
            if (pos_ >= data_.size())
            {
                return std::char_traits<char>::eof();
            }
            const std::size_t n = (std::min)(chunk_, data_.size() - pos_);
            buf_.assign(data_.begin() + pos_, data_.begin() + pos_ + n);
            pos_ += n;
        }
        const char c = buf_.front();
        buf_.erase(buf_.begin());
        return std::char_traits<char>::to_int_type(c);
    }

  private:
    std::string data_;
    std::size_t chunk_;
    std::size_t pos_ = 0;
    std::string buf_;
};

void check_equal(const std::string& a, const std::string& b)
{
    std::istringstream sa(a);
    std::istringstream sb(b);
    const json_diff d = nlohmann::compare_json_stream(sa, sb);
    CHECK(d.kind == json_diff_kind::equal);
}

void check_diff(const std::string& a, const std::string& b, json_diff_kind expected_kind,
                const std::string& expected_pointer, const std::string& expected_left,
                const std::string& expected_right)
{
    std::istringstream sa(a);
    std::istringstream sb(b);
    const json_diff d = nlohmann::compare_json_stream(sa, sb);
    CHECK(d.kind == expected_kind);
    CHECK(d.pointer == expected_pointer);
    CHECK(d.left_value == expected_left);
    CHECK(d.right_value == expected_right);
}

} // namespace

TEST_CASE("streaming comparison - equal documents")
{
    check_equal(R"({"a": [1, 2, 3], "b": "x"})", R"({"a": [1, 2, 3], "b": "x"})");
    check_equal(R"({"s": "abc"})", R"({"s": "abc"})");
    check_equal(R"(true)", R"(true)");
    check_equal(R"(false)", R"(false)");
    check_equal(R"(null)", R"(null)");
    check_equal(R"([1, 2, 3])", R"([1, 2, 3])");
    check_equal(R"({})", R"({})");
    check_equal(R"([])", R"([])");
}

TEST_CASE("streaming comparison - deep array value change")
{
    const std::string a = R"({"a": [1, 2, 3, 4, 5]})";
    const std::string b = R"({"a": [1, 2, 9, 4, 5]})";
    std::istringstream sa(a);
    std::istringstream sb(b);
    const json_diff d = nlohmann::compare_json_stream(sa, sb);
    CHECK(d.kind == json_diff_kind::value_mismatch);
    CHECK(d.pointer == "/a/2");
    CHECK(d.left_value == "3");
    CHECK(d.right_value == "9");
    CHECK(d.left_offset == d.right_offset);
    CHECK(d.left_offset == a.find("3", 5));
}

TEST_CASE("streaming comparison - member order change")
{
    const std::string a = R"({"a": 1, "b": 2})";
    const std::string b = R"({"b": 2, "a": 1})";
    std::istringstream sa(a);
    std::istringstream sb(b);
    const json_diff d = nlohmann::compare_json_stream(sa, sb);
    CHECK(d.kind == json_diff_kind::key_mismatch);
    CHECK(d.pointer == "");
    CHECK(d.left_value == "a");
    CHECK(d.right_value == "b");
    CHECK(d.left_offset == a.find(R"("a")"));
    CHECK(d.right_offset == b.find(R"("b")"));
}

TEST_CASE("streaming comparison - equivalent escaped strings")
{
    check_equal(R"("hello\tworld")", R"("hello\u0009world")");
    check_equal(R"({"a": "x\/y"})", R"({"a": "x/y"})");
    check_equal(R"("abc")", R"("abc")");
    check_diff(R"("abc")", R"("abd")", json_diff_kind::value_mismatch, "", "abc", "abd");
}

TEST_CASE("streaming comparison - different number types")
{
    check_equal(R"(1)", R"(1.0)");
    check_equal(R"(1.0)", R"(1)");
    check_equal(R"(-1)", R"(-1.0)");
    check_equal(R"(0)", R"(0.0)");
    check_diff(R"(1)", R"(2)", json_diff_kind::value_mismatch, "", "1", "2");
    check_diff(R"(1.0)", R"(2.0)", json_diff_kind::value_mismatch, "", "1.0", "2.0");
    check_diff(R"(1)", R"("1")", json_diff_kind::type_mismatch, "", "1", "1");
    check_diff(R"(true)", R"(1)", json_diff_kind::type_mismatch, "", "true", "1");
}

TEST_CASE("streaming comparison - empty containers")
{
    check_equal(R"({})", R"({})");
    check_equal(R"([])", R"([])");
    check_diff(R"({})", R"([])", json_diff_kind::type_mismatch, "", "{", "[");
    check_diff(R"([])", R"({})", json_diff_kind::type_mismatch, "", "[", "{");
}

TEST_CASE("streaming comparison - syntax error priority")
{
    check_diff(R"([1,2,3])", R"([1,2,3x)", json_diff_kind::syntax_error, "", "", "");
    check_diff(R"({"a":1})", R"({"a":1}x)", json_diff_kind::syntax_error, "", "", "");
    check_diff(R"([1,2,3)", R"([1,2,3])", json_diff_kind::syntax_error, "", "", "");
    check_diff(R"({"a":1})", R"({"a":1)", json_diff_kind::syntax_error, "", "", "");
    check_diff(R"(1 2)", R"(1 2)", json_diff_kind::syntax_error, "", "", "");
}

TEST_CASE("streaming comparison - chunked reading")
{
    const std::string a = R"({"a": [1, 2, 3], "b": "x"})";
    const std::string b = R"({"a": [1, 2, 4], "b": "x"})";
    for (std::size_t chunk = 1; chunk <= 8; ++chunk)
    {
        chunked_input_adapter ca(a, chunk);
        chunked_input_adapter cb(b, chunk);
        const json_diff d = nlohmann::compare_json_stream_adapter(std::move(ca), std::move(cb));
        CHECK(d.kind == json_diff_kind::value_mismatch);
        CHECK(d.pointer == "/a/2");
        CHECK(d.left_value == "3");
        CHECK(d.right_value == "4");
    }
}

TEST_CASE("streaming comparison - nesting limit")
{
    const int depth = 20;
    std::string deep;
    for (int i = 0; i < depth; ++i)
    {
        deep += '[';
    }
    deep += '1';
    for (int i = 0; i < depth; ++i)
    {
        deep += ']';
    }

    SECTION("exceeded")
    {
        json_compare_options opts;
        opts.max_nesting_depth = 10;
        std::istringstream sa(deep);
        std::istringstream sb(deep);
        const json_diff d = nlohmann::compare_json_stream(sa, sb, opts);
        CHECK(d.kind == json_diff_kind::nesting_too_deep);
        CHECK_FALSE(d.error.empty());
    }

    SECTION("within limit")
    {
        json_compare_options opts;
        opts.max_nesting_depth = 100;
        std::istringstream sa(deep);
        std::istringstream sb(deep);
        const json_diff d = nlohmann::compare_json_stream(sa, sb, opts);
        CHECK(d.kind == json_diff_kind::equal);
    }
}

TEST_CASE("streaming comparison - truncated value display")
{
    const std::string long_str(200, 'x');
    const std::string a = R"({"s": ")" + long_str + R"("})";
    const std::string b = R"({"s": ")" + std::string(200, 'y') + R"("})";
    std::istringstream sa(a);
    std::istringstream sb(b);
    const json_diff d = nlohmann::compare_json_stream(sa, sb);
    CHECK(d.kind == json_diff_kind::value_mismatch);
    CHECK(d.left_value.size() <= 83);
    CHECK(d.left_value.substr(80) == "...");
    CHECK(d.right_value.size() <= 83);
    CHECK(d.right_value.substr(80) == "...");
}

TEST_CASE("streaming comparison - array length mismatch")
{
    check_diff(R"([1,2,3])", R"([1,2])", json_diff_kind::array_length_mismatch, "", ",", "]");
    check_diff(R"([1,2])", R"([1,2,3])", json_diff_kind::array_length_mismatch, "", "]", ",");
    check_diff(R"({"a":[1,2,3]})", R"({"a":[1,2]})", json_diff_kind::array_length_mismatch, "/a", ",", "]");
}

TEST_CASE("streaming comparison - object length mismatch")
{
    check_diff(R"({"a":1,"b":2})", R"({"a":1})", json_diff_kind::object_length_mismatch, "", ",", "}");
    check_diff(R"({"a":1})", R"({"a":1,"b":2})", json_diff_kind::object_length_mismatch, "", "}", ",");
}

TEST_CASE("streaming comparison - syntax error in one side only")
{
    std::istringstream sa(R"([1,2,3x)");
    std::istringstream sb(R"([1,2,3])");
    const json_diff d = nlohmann::compare_json_stream(sa, sb);
    CHECK(d.kind == json_diff_kind::syntax_error);
    CHECK_FALSE(d.error.empty());
}
