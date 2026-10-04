//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <nlohmann/json.hpp>
#include <nlohmann/detail/input/lexer.hpp>
#include <nlohmann/detail/input/input_adapters.hpp>
#include <nlohmann/detail/value_t.hpp>

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

NLOHMANN_JSON_NAMESPACE_BEGIN

enum class json_diff_kind
{
    equal,
    type_mismatch,
    value_mismatch,
    key_mismatch,
    array_length_mismatch,
    object_length_mismatch,
    nesting_too_deep,
    syntax_error
};

struct json_diff
{
    json_diff_kind kind = json_diff_kind::equal;
    std::string pointer;
    std::string left_value;
    std::string right_value;
    std::size_t left_line = 0;
    std::size_t left_column = 0;
    std::size_t left_offset = 0;
    std::size_t right_line = 0;
    std::size_t right_column = 0;
    std::size_t right_offset = 0;
    std::string error;
};

struct json_compare_options
{
    std::size_t max_nesting_depth = 1024;
    std::size_t max_value_display = 80;
};

namespace detail
{

template<typename Adapter1, typename Adapter2>
class basic_json_stream_comparator
{
  public:
    basic_json_stream_comparator(Adapter1 a1, Adapter2 a2, json_compare_options opts)
        : left_(std::move(a1))
        , right_(std::move(a2))
        , opts_(opts)
    {}

    json_diff compare()
    {
        compare_value();
        if (terminal_)
        {
            return result_;
        }

        const auto lt = left_.scan();
        const auto rt = right_.scan();
        if (lt != lexer_t::token_type::end_of_input)
        {
            fail_syntax("trailing content after root value");
            return result_;
        }
        if (rt != lexer_t::token_type::end_of_input)
        {
            fail_syntax("trailing content after root value");
            return result_;
        }

        return result_;
    }

  private:
    using lexer_t = lexer<nlohmann::json, Adapter1>;
    using lexer2_t = lexer<nlohmann::json, Adapter2>;
    using token_type = typename lexer_t::token_type;
    using number_integer_t = typename nlohmann::json::number_integer_t;
    using number_unsigned_t = typename nlohmann::json::number_unsigned_t;
    using number_float_t = typename nlohmann::json::number_float_t;

    template<typename Lexer>
    bool scan_token(Lexer& lex, token_type& out)
    {
        out = lex.scan();
        if (out == token_type::parse_error)
        {
            fail_syntax(lex.get_error_message());
            return false;
        }
        if (out == token_type::end_of_input)
        {
            fail_syntax("unexpected end of input");
            return false;
        }
        return true;
    }

    void compare_value()
    {
        token_type lt;
        token_type rt;
        if (!scan_token(left_, lt))
        {
            return;
        }
        if (!scan_token(right_, rt))
        {
            return;
        }
        compare_value_tokens(lt, rt);
    }

    void compare_value_tokens(token_type lt, token_type rt)
    {
        if (terminal_)
        {
            return;
        }

        const bool l_container = is_container(lt);
        const bool r_container = is_container(rt);

        if (l_container != r_container)
        {
            record_diff(json_diff_kind::type_mismatch, lt, rt);
            skip_value(left_, lt);
            skip_value(right_, rt);
            return;
        }

        if (l_container)
        {
            if (lt != rt)
            {
                record_diff(json_diff_kind::type_mismatch, lt, rt);
                skip_value(left_, lt);
                skip_value(right_, rt);
                return;
            }
            if (lt == token_type::begin_array)
            {
                compare_array();
            }
            else
            {
                compare_object();
            }
            return;
        }

        const bool l_num = is_number(lt);
        const bool r_num = is_number(rt);

        if (l_num && r_num)
        {
            if (!numbers_equal(lt, rt))
            {
                record_diff(json_diff_kind::value_mismatch, lt, rt);
            }
            return;
        }

        if (lt != rt)
        {
            record_diff(json_diff_kind::type_mismatch, lt, rt);
            return;
        }

        switch (lt)
        {
            case token_type::literal_true:
            case token_type::literal_false:
            case token_type::literal_null:
                break;

            case token_type::value_string:
                if (left_.get_string() != right_.get_string())
                {
                    record_diff(json_diff_kind::value_mismatch, lt, rt);
                }
                break;

            default:
                break;
        }
    }

    bool numbers_equal(token_type lt, token_type rt)
    {
        const bool l_float = (lt == token_type::value_float);
        const bool r_float = (rt == token_type::value_float);

        if (!l_float && !r_float)
        {
            const bool l_int = (lt == token_type::value_integer);
            const bool r_int = (rt == token_type::value_integer);

            if (l_int && r_int)
            {
                return left_.get_number_integer() == right_.get_number_integer();
            }
            if (!l_int && !r_int)
            {
                return left_.get_number_unsigned() == right_.get_number_unsigned();
            }

            const number_unsigned_t u = l_int ? right_.get_number_unsigned() : left_.get_number_unsigned();
            const number_integer_t i = l_int ? left_.get_number_integer() : right_.get_number_integer();
            if (i < 0)
            {
                return false;
            }
            return static_cast<number_unsigned_t>(i) == u;
        }

        if (l_float && r_float)
        {
            return left_.get_number_float() == right_.get_number_float();
        }

        if (l_float)
        {
            if (rt == token_type::value_integer)
            {
                return detail::compare_integer_with_float(right_.get_number_integer(), left_.get_number_float()) == static_cast<number_float_t>(0);
            }
            return detail::compare_integer_with_float(right_.get_number_unsigned(), left_.get_number_float()) == static_cast<number_float_t>(0);
        }

        if (lt == token_type::value_integer)
        {
            return detail::compare_integer_with_float(left_.get_number_integer(), right_.get_number_float()) == static_cast<number_float_t>(0);
        }
        return detail::compare_integer_with_float(left_.get_number_unsigned(), right_.get_number_float()) == static_cast<number_float_t>(0);
    }

    void compare_array()
    {
        if (!enter_container())
        {
            return;
        }
        path_.push_back({true, 0, {}});

        while (true)
        {
            token_type lt;
            token_type rt;
            if (!scan_token(left_, lt))
            {
                return;
            }
            if (!scan_token(right_, rt))
            {
                return;
            }

            if (lt == token_type::end_array && rt == token_type::end_array)
            {
                leave_container();
                return;
            }

            if (lt == token_type::end_array || rt == token_type::end_array)
            {
                record_diff(json_diff_kind::array_length_mismatch, lt, rt);
                if (lt != token_type::end_array)
                {
                    skip_array_remainder(left_, lt);
                }
                if (rt != token_type::end_array)
                {
                    skip_array_remainder(right_, rt);
                }
                leave_container();
                return;
            }

            compare_value_tokens(lt, rt);
            if (terminal_)
            {
                return;
            }

            if (!scan_token(left_, lt))
            {
                return;
            }
            if (!scan_token(right_, rt))
            {
                return;
            }

            if (lt == token_type::end_array && rt == token_type::end_array)
            {
                leave_container();
                return;
            }

            if (lt == token_type::value_separator && rt == token_type::value_separator)
            {
                ++path_.back().index;
                continue;
            }

            if (lt == token_type::value_separator || rt == token_type::value_separator)
            {
                record_diff(json_diff_kind::array_length_mismatch, lt, rt);
                if (lt == token_type::value_separator)
                {
                    skip_array(left_);
                }
                if (rt == token_type::value_separator)
                {
                    skip_array(right_);
                }
                leave_container();
                return;
            }

            fail_syntax("expected ',' or ']'");
            return;
        }
    }

    void compare_object()
    {
        if (!enter_container())
        {
            return;
        }
        path_.push_back({false, 0, {}});

        while (true)
        {
            token_type lt;
            token_type rt;
            if (!scan_token(left_, lt))
            {
                return;
            }
            if (!scan_token(right_, rt))
            {
                return;
            }

            if (lt == token_type::end_object && rt == token_type::end_object)
            {
                leave_container();
                return;
            }

            if (lt == token_type::end_object || rt == token_type::end_object)
            {
                record_diff(json_diff_kind::object_length_mismatch, lt, rt);
                if (lt != token_type::end_object)
                {
                    skip_object_remainder(left_, lt);
                }
                if (rt != token_type::end_object)
                {
                    skip_object_remainder(right_, rt);
                }
                leave_container();
                return;
            }

            if (lt != token_type::value_string || rt != token_type::value_string)
            {
                fail_syntax("expected string key");
                return;
            }

            if (left_.get_string() != right_.get_string())
            {
                record_diff(json_diff_kind::key_mismatch, lt, rt);

                if (!scan_token(left_, lt))
                {
                    return;
                }
                if (!scan_token(right_, rt))
                {
                    return;
                }
                if (lt != token_type::name_separator || rt != token_type::name_separator)
                {
                    fail_syntax("expected ':'");
                    return;
                }
                if (!scan_token(left_, lt))
                {
                    return;
                }
                if (!scan_token(right_, rt))
                {
                    return;
                }
                skip_value(left_, lt);
                skip_value(right_, rt);
                if (terminal_)
                {
                    return;
                }

                if (!scan_token(left_, lt))
                {
                    return;
                }
                if (!scan_token(right_, rt))
                {
                    return;
                }

                if (lt == token_type::end_object && rt == token_type::end_object)
                {
                    leave_container();
                    return;
                }

                if (lt == token_type::value_separator && rt == token_type::value_separator)
                {
                    ++path_.back().index;
                    continue;
                }

                if (lt == token_type::value_separator || rt == token_type::value_separator)
                {
                    record_diff(json_diff_kind::object_length_mismatch, lt, rt);
                    if (lt == token_type::value_separator)
                    {
                        skip_object(left_);
                    }
                    if (rt == token_type::value_separator)
                    {
                        skip_object(right_);
                    }
                    leave_container();
                    return;
                }

                fail_syntax("expected ',' or '}'");
                return;
            }

            path_.back().key = left_.get_string();

            if (!scan_token(left_, lt))
            {
                return;
            }
            if (!scan_token(right_, rt))
            {
                return;
            }
            if (lt != token_type::name_separator || rt != token_type::name_separator)
            {
                fail_syntax("expected ':'");
                return;
            }

            compare_value();
            if (terminal_)
            {
                return;
            }

            if (!scan_token(left_, lt))
            {
                return;
            }
            if (!scan_token(right_, rt))
            {
                return;
            }

            if (lt == token_type::end_object && rt == token_type::end_object)
            {
                leave_container();
                return;
            }

            if (lt == token_type::value_separator && rt == token_type::value_separator)
            {
                ++path_.back().index;
                continue;
            }

            if (lt == token_type::value_separator || rt == token_type::value_separator)
            {
                record_diff(json_diff_kind::object_length_mismatch, lt, rt);
                if (lt == token_type::value_separator)
                {
                    skip_object(left_);
                }
                if (rt == token_type::value_separator)
                {
                    skip_object(right_);
                }
                leave_container();
                return;
            }

            fail_syntax("expected ',' or '}'");
            return;
        }
    }

    template<typename Lexer>
    void skip_value(Lexer& lex, token_type first)
    {
        if (terminal_)
        {
            return;
        }
        switch (first)
        {
            case token_type::begin_array:
                skip_array(lex);
                break;
            case token_type::begin_object:
                skip_object(lex);
                break;
            default:
                break;
        }
    }

    template<typename Lexer>
    void skip_array(Lexer& lex)
    {
        while (true)
        {
            token_type t;
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t == token_type::end_array)
            {
                return;
            }
            skip_value(lex, t);
            if (terminal_)
            {
                return;
            }
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t == token_type::end_array)
            {
                return;
            }
            if (t != token_type::value_separator)
            {
                fail_syntax(lex, "expected ',' or ']'");
                return;
            }
        }
    }

    template<typename Lexer>
    void skip_array_remainder(Lexer& lex, token_type first)
    {
        skip_value(lex, first);
        while (true)
        {
            token_type t;
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t == token_type::end_array)
            {
                return;
            }
            if (t != token_type::value_separator)
            {
                fail_syntax(lex, "expected ',' or ']'");
                return;
            }
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t == token_type::end_array)
            {
                fail_syntax(lex, "unexpected end of array");
                return;
            }
            skip_value(lex, t);
            if (terminal_)
            {
                return;
            }
        }
    }

    template<typename Lexer>
    void skip_object(Lexer& lex)
    {
        while (true)
        {
            token_type t;
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t == token_type::end_object)
            {
                return;
            }
            if (t != token_type::value_string)
            {
                fail_syntax(lex, "expected string key");
                return;
            }
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t != token_type::name_separator)
            {
                fail_syntax(lex, "expected ':'");
                return;
            }
            if (!scan_token(lex, t))
            {
                return;
            }
            skip_value(lex, t);
            if (terminal_)
            {
                return;
            }
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t == token_type::end_object)
            {
                return;
            }
            if (t != token_type::value_separator)
            {
                fail_syntax(lex, "expected ',' or '}'");
                return;
            }
        }
    }

    template<typename Lexer>
    void skip_object_remainder(Lexer& lex, token_type first)
    {
        if (first != token_type::value_string)
        {
            fail_syntax(lex, "expected string key");
            return;
        }
        token_type t;
        if (!scan_token(lex, t))
        {
            return;
        }
        if (t != token_type::name_separator)
        {
            fail_syntax(lex, "expected ':'");
            return;
        }
        if (!scan_token(lex, t))
        {
            return;
        }
        skip_value(lex, t);
        if (terminal_)
        {
            return;
        }
        while (true)
        {
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t == token_type::end_object)
            {
                return;
            }
            if (t != token_type::value_separator)
            {
                fail_syntax(lex, "expected ',' or '}'");
                return;
            }
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t == token_type::end_object)
            {
                fail_syntax(lex, "unexpected end of object");
                return;
            }
            if (t != token_type::value_string)
            {
                fail_syntax(lex, "expected string key");
                return;
            }
            if (!scan_token(lex, t))
            {
                return;
            }
            if (t != token_type::name_separator)
            {
                fail_syntax(lex, "expected ':'");
                return;
            }
            if (!scan_token(lex, t))
            {
                return;
            }
            skip_value(lex, t);
            if (terminal_)
            {
                return;
            }
        }
    }

    bool enter_container()
    {
        ++depth_;
        if (depth_ > opts_.max_nesting_depth)
        {
            fail_nesting("nesting depth exceeds configured limit");
            return false;
        }
        return true;
    }

    void leave_container()
    {
        --depth_;
        path_.pop_back();
    }

    void record_diff(json_diff_kind kind, token_type lt, token_type rt)
    {
        if (have_diff_)
        {
            return;
        }
        have_diff_ = true;
        result_.kind = kind;
        // key and length mismatches point to the containing container, not the
        // current member/element
        result_.pointer = (kind == json_diff_kind::key_mismatch
                           || kind == json_diff_kind::array_length_mismatch
                           || kind == json_diff_kind::object_length_mismatch)
                          ? container_pointer()
                          : current_pointer();
        result_.left_value = describe_token(left_, lt);
        result_.right_value = describe_token(right_, rt);
        capture_position(left_, result_.left_line, result_.left_column, result_.left_offset);
        capture_position(right_, result_.right_line, result_.right_column, result_.right_offset);
    }

    void fail_syntax(const char* msg)
    {
        if (terminal_)
        {
            return;
        }
        terminal_ = true;
        result_.kind = json_diff_kind::syntax_error;
        result_.error = msg;
        capture_position(left_, result_.left_line, result_.left_column, result_.left_offset);
        capture_position(right_, result_.right_line, result_.right_column, result_.right_offset);
    }

    template<typename Lexer>
    void fail_syntax(Lexer& /*lex*/, const char* msg)
    {
        fail_syntax(msg);
    }

    void fail_nesting(const char* msg)
    {
        if (terminal_)
        {
            return;
        }
        terminal_ = true;
        result_.kind = json_diff_kind::nesting_too_deep;
        result_.error = msg;
    }

    std::string current_pointer() const
    {
        std::string p;
        for (const auto& f : path_)
        {
            p += '/';
            if (f.is_array)
            {
                p += std::to_string(f.index);
            }
            else
            {
                p += escape_json_pointer(f.key);
            }
        }
        return p;
    }

    /// JSON Pointer to the current container (excluding the current frame).
    /// Used for key and length mismatches, which are properties of the
    /// container rather than a specific member or element.
    std::string container_pointer() const
    {
        std::string p;
        for (std::size_t i = 0; i + 1 < path_.size(); ++i)
        {
            const auto& f = path_[i];
            p += '/';
            if (f.is_array)
            {
                p += std::to_string(f.index);
            }
            else
            {
                p += escape_json_pointer(f.key);
            }
        }
        return p;
    }

    static std::string escape_json_pointer(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (const char c : s)
        {
            if (c == '~')
            {
                out += "~0";
            }
            else if (c == '/')
            {
                out += "~1";
            }
            else
            {
                out += c;
            }
        }
        return out;
    }

    template<typename Lexer>
    std::string describe_token(Lexer& lex, token_type t) const
    {
        switch (t)
        {
            case token_type::begin_array:
                return "[";
            case token_type::begin_object:
                return "{";
            case token_type::end_array:
                return "]";
            case token_type::end_object:
                return "}";
            case token_type::name_separator:
                return ":";
            case token_type::value_separator:
                return ",";
            case token_type::literal_true:
                return "true";
            case token_type::literal_false:
                return "false";
            case token_type::literal_null:
                return "null";
            case token_type::value_string:
                return truncate(lex.get_string());
            case token_type::value_unsigned:
            case token_type::value_integer:
            case token_type::value_float:
                return truncate(lex.get_token_string());
            default:
                return lexer_t::token_type_name(t);
        }
    }

    static std::string truncate(const std::string& s, std::size_t max_len)
    {
        if (s.size() <= max_len)
        {
            return s;
        }
        return s.substr(0, max_len) + "...";
    }

    std::string truncate(const std::string& s) const
    {
        return truncate(s, opts_.max_value_display);
    }

    template<typename Lexer>
    void capture_position(Lexer& lex, std::size_t& line, std::size_t& column, std::size_t& offset) const
    {
        const auto pos = lex.get_position();
        const std::string token = lex.get_token_string();
        const std::size_t token_len = token.size();

        line = pos.lines_read + 1;
        const std::size_t end_col = pos.chars_read_current_line;
        column = (end_col >= token_len) ? (end_col - token_len + 1) : 1;
        offset = (pos.chars_read_total >= token_len) ? (pos.chars_read_total - token_len) : 0;
    }

    static bool is_container(token_type t)
    {
        return t == token_type::begin_array || t == token_type::begin_object;
    }

    static bool is_number(token_type t)
    {
        return t == token_type::value_unsigned
               || t == token_type::value_integer
               || t == token_type::value_float;
    }

    lexer_t left_;
    lexer2_t right_;
    json_compare_options opts_;
    json_diff result_;
    bool terminal_ = false;
    bool have_diff_ = false;
    std::size_t depth_ = 0;

    struct path_frame
    {
        bool is_array;
        std::size_t index;
        std::string key;
    };
    std::vector<path_frame> path_;
};

} // namespace detail

template<typename Input1, typename Input2>
json_diff compare_json_stream(Input1&& in1, Input2&& in2,
                              json_compare_options opts = json_compare_options())
{
    return compare_json_stream_adapter(
        detail::input_adapter(std::forward<Input1>(in1)),
        detail::input_adapter(std::forward<Input2>(in2)),
        opts);
}

template<typename Adapter1, typename Adapter2>
json_diff compare_json_stream_adapter(Adapter1 a1, Adapter2 a2,
                                      json_compare_options opts = json_compare_options())
{
    detail::basic_json_stream_comparator<Adapter1, Adapter2> cmp(std::move(a1), std::move(a2), opts);
    return cmp.compare();
}

NLOHMANN_JSON_NAMESPACE_END
