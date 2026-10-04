//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | |   | |  version 3.12.0
// |_____|_____|_|_|_|___|_|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <string> // string, to_string
#include <utility> // forward, move
#include <vector> // vector

#include <nlohmann/json.hpp>

// local branch-prediction macros (the library's JSON_HEDLEY_* macros are
// undefined again by macro_unscope.hpp at the end of json.hpp)
#if defined(__GNUC__) || defined(__clang__)
    #define JSON_DIFF_LIKELY(x) __builtin_expect(!!(x), 1)
    #define JSON_DIFF_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define JSON_DIFF_LIKELY(x) (x)
    #define JSON_DIFF_UNLIKELY(x) (x)
#endif

NLOHMANN_JSON_NAMESPACE_BEGIN

/*!
@brief streaming JSON comparison

This namespace implements an optional, SAX-style comparison of two JSON
inputs. Both inputs are consumed token by token with two lexers running in
lockstep; no DOM is ever built. Memory usage is limited to the current
nesting path and a bounded amount of token information, so arbitrarily large
inputs can be compared.

Comparison semantics:
- object member order is significant ({"a":1,"b":2} != {"b":2,"a":1})
- array elements are compared in order
- strings are compared by their decoded value ("A" == "A")
- numbers are compared with the same rules as nlohmann::json::operator==
  after the library's usual type parsing (e.g. 1 == 1.0 == 1e0)

The result reports the JSON Pointer (RFC 6901) of the first difference, the
kind of difference, and the token positions of both sides. Long values are
truncated in the report; the compared documents are never copied into the
result. After the first difference is found, both inputs are still consumed
to the end to validate their syntax: a syntax error on either side takes
priority over any content difference and is reported instead.
*/
namespace json_diff
{

/// the kind of the first detected difference
enum class diff_kind
{
    none,                 ///< no difference
    type_mismatch,        ///< different JSON types at the same position
    value_mismatch,       ///< same JSON type, different value
    key_mismatch,         ///< different object keys (member order is significant)
    object_size_mismatch, ///< one object has more members than the other
    array_size_mismatch   ///< one array has more elements than the other
};

/// the overall outcome of a comparison
enum class status
{
    equal,          ///< both inputs are equal (and syntactically valid)
    different,      ///< inputs differ; see diff_kind and json_pointer
    syntax_error,   ///< at least one input is not valid JSON (takes priority over content differences)
    depth_exceeded  ///< the configured nesting limit was exceeded
};

/// the position of a token within one input
struct token_position
{
    std::size_t offset = 0; ///< number of bytes consumed so far: the offset just past the token's last byte
    std::size_t line = 1;   ///< 1-based line of the token's last byte
    std::size_t column = 0; ///< 1-based column of the token's last byte
};

/// bounded information about one side's token at the difference or error
struct token_info
{
    token_position position {};
    std::string preview {};  ///< printable preview of the token, truncated to options::max_preview_length
    bool truncated = false;  ///< whether the preview was truncated
};

/// tuning knobs for the comparison
struct options
{
    std::size_t max_depth = 0;          ///< maximum nesting of arrays/objects; 0 = unlimited
    std::size_t max_preview_length = 64;///< maximum length of a token preview in the report
};

/// the result of a streaming comparison
struct result
{
    status outcome = status::equal;   ///< overall outcome
    diff_kind kind = diff_kind::none; ///< kind of the first difference (if outcome == status::different)
    std::string json_pointer {};      ///< RFC 6901 JSON Pointer to the first difference ("" for the root value)
    token_info lhs {};                ///< token of the first input at the difference/error
    token_info rhs {};                ///< token of the second input at the difference/error
    int error_side = 0;               ///< for status::syntax_error: 1 = first input, 2 = second input
    std::string message {};           ///< human-readable description

    /// whether both inputs are equal
    bool equal() const noexcept
    {
        return outcome == status::equal;
    }

    /// implicit conversion to bool: true iff both inputs are equal
    explicit operator bool() const noexcept
    {
        return equal();
    }
};

/// return the name of a diff_kind value
inline const char* to_string(const diff_kind k) noexcept
{
    switch (k)
    {
        case diff_kind::none: return "none";
        case diff_kind::type_mismatch: return "type_mismatch";
        case diff_kind::value_mismatch: return "value_mismatch";
        case diff_kind::key_mismatch: return "key_mismatch";
        case diff_kind::object_size_mismatch: return "object_size_mismatch";
        case diff_kind::array_size_mismatch: return "array_size_mismatch";
    }
    return "unknown"; // LCOV_EXCL_LINE
}

/// return the name of a status value
inline const char* to_string(const status s) noexcept
{
    switch (s)
    {
        case status::equal: return "equal";
        case status::different: return "different";
        case status::syntax_error: return "syntax_error";
        case status::depth_exceeded: return "depth_exceeded";
    }
    return "unknown"; // LCOV_EXCL_LINE
}

namespace detail
{

/// the SAX-style events produced by the event stream
enum class event_kind : std::uint8_t
{
    null_value,
    boolean,
    number_integer,
    number_unsigned,
    number_float,
    string_value,
    key,
    begin_object,
    end_object,
    begin_array,
    end_array,
    end_of_input
};

/// one SAX-style event with its token position and (bounded) payload
template<typename BasicJsonType>
struct event
{
    using string_t = typename BasicJsonType::string_t;
    using number_integer_t = typename BasicJsonType::number_integer_t;
    using number_unsigned_t = typename BasicJsonType::number_unsigned_t;
    using number_float_t = typename BasicJsonType::number_float_t;

    event_kind kind = event_kind::end_of_input;
    nlohmann::detail::position_t position {};  ///< position of the token's last byte
    bool boolean_value = false;
    number_integer_t integer_value = 0;
    number_unsigned_t unsigned_value = 0;
    number_float_t float_value = 0;
    string_t string_value {};                  ///< decoded value for string_value/key events
    std::string raw {};                        ///< raw token text for number events
};

/*!
@brief turns one input into a stream of SAX-style events

This is a minimal iterative JSON parser built directly on the library's own
lexer. It validates the full JSON grammar (the lexer validates tokens,
including string escapes and number formats) and reports the position of
every event. Only the current nesting stack is kept.
*/
template<typename BasicJsonType, typename InputAdapter>
class event_stream
{
    using lexer_t = nlohmann::detail::lexer<BasicJsonType, InputAdapter>;
    using token_type = typename lexer_t::token_type;
    using string_t = typename BasicJsonType::string_t;

  public:
    using number_integer_t = typename BasicJsonType::number_integer_t;
    using number_unsigned_t = typename BasicJsonType::number_unsigned_t;
    using number_float_t = typename BasicJsonType::number_float_t;
    using position_t = nlohmann::detail::position_t;
    using event = json_diff::detail::event<BasicJsonType>;

    explicit event_stream(InputAdapter&& adapter)
        : m_lexer(std::move(adapter))
    {}

    /// stop copying decoded string values into events (validate-only mode)
    void discard_values() noexcept
    {
        m_discard = true;
    }

    /// position of the most recently scanned token (valid after exceptions, too)
    position_t current_position() const noexcept
    {
        return m_lexer.get_position();
    }

    /// produce the next event; throws nlohmann::detail::parse_error on invalid input
    event next()
    {
        for (;;)
        {
            const token_type tok = m_lexer.scan();
            const position_t pos = m_lexer.get_position();

            if (JSON_DIFF_UNLIKELY(tok == token_type::parse_error))
            {
                throw nlohmann::detail::parse_error::create(101, pos,
                        nlohmann::detail::concat("syntax error - invalid token: ", m_lexer.get_error_message(),
                               "; last read: '", m_lexer.get_token_string(), "'"), nullptr);
            }

            if (m_stack.empty())
            {
                if (!m_root_done)
                {
                    // expecting the root value
                    if (JSON_DIFF_UNLIKELY(tok == token_type::end_of_input))
                    {
                        throw nlohmann::detail::parse_error::create(101, pos,
                                "attempting to parse an empty input; check that your input string or stream contains the expected JSON", nullptr);
                    }
                    event ev;
                    if (JSON_DIFF_LIKELY(value_event(tok, ev, pos)))
                    {
                        return ev;
                    }
                    throw unexpected(tok, "value");
                }

                // the root value is complete; only the end of the input may follow
                if (JSON_DIFF_LIKELY(tok == token_type::end_of_input))
                {
                    event ev;
                    ev.kind = event_kind::end_of_input;
                    ev.position = pos;
                    return ev;
                }
                throw unexpected(tok, "end of input");
            }

            frame& f = m_stack.back();
            switch (f.state_)
            {
                case state::object_key_or_end:
                case state::object_key:
                {
                    if (tok == token_type::value_string)
                    {
                        f.state_ = state::object_colon;
                        event ev;
                        ev.kind = event_kind::key;
                        ev.position = pos;
                        if (!m_discard)
                        {
                            ev.string_value = std::move(m_lexer.get_string());
                        }
                        return ev;
                    }
                    if (f.state_ == state::object_key_or_end && tok == token_type::end_object)
                    {
                        return close_container(event_kind::end_object, pos);
                    }
                    throw unexpected(tok, f.state_ == state::object_key_or_end ? "object key or '}'" : "object key");
                }

                case state::object_colon:
                {
                    if (JSON_DIFF_LIKELY(tok == token_type::name_separator))
                    {
                        f.state_ = state::object_value;
                        continue;
                    }
                    throw unexpected(tok, "':' separator");
                }

                case state::object_value:
                case state::array_value_or_end:
                case state::array_value:
                {
                    event ev;
                    if (JSON_DIFF_LIKELY(value_event(tok, ev, pos)))
                    {
                        return ev;
                    }
                    if (f.state_ == state::array_value_or_end && tok == token_type::end_array)
                    {
                        return close_container(event_kind::end_array, pos);
                    }
                    throw unexpected(tok, "value");
                }

                case state::object_comma_or_end:
                {
                    if (tok == token_type::value_separator)
                    {
                        f.state_ = state::object_key;
                        continue;
                    }
                    if (tok == token_type::end_object)
                    {
                        return close_container(event_kind::end_object, pos);
                    }
                    throw unexpected(tok, "',' or '}'");
                }

                case state::array_comma_or_end:
                {
                    if (tok == token_type::value_separator)
                    {
                        f.state_ = state::array_value;
                        continue;
                    }
                    if (tok == token_type::end_array)
                    {
                        return close_container(event_kind::end_array, pos);
                    }
                    throw unexpected(tok, "',' or ']'");
                }
            }
        }
    }

  private:
    enum class state : std::uint8_t
    {
        object_key_or_end,   ///< after '{': a key or '}' may follow
        object_key,          ///< after ',': a key must follow
        object_colon,        ///< after a key: ':' must follow
        object_value,        ///< after ':': a value must follow
        object_comma_or_end, ///< after a member value: ',' or '}' must follow
        array_value_or_end,  ///< after '[': a value or ']' may follow
        array_value,         ///< after ',': a value must follow
        array_comma_or_end   ///< after an element: ',' or ']' must follow
    };

    struct frame
    {
        bool is_object;
        state state_;
    };

    /// handle a token that may start a value; returns false for other tokens
    bool value_event(const token_type tok, event& ev, const position_t& pos)
    {
        ev.position = pos;
        switch (tok)
        {
            case token_type::literal_null:
                ev.kind = event_kind::null_value;
                break;
            case token_type::literal_true:
                ev.kind = event_kind::boolean;
                ev.boolean_value = true;
                break;
            case token_type::literal_false:
                ev.kind = event_kind::boolean;
                break;
            case token_type::value_integer:
                ev.kind = event_kind::number_integer;
                ev.integer_value = m_lexer.get_number_integer();
                ev.raw = m_lexer.get_token_string();
                break;
            case token_type::value_unsigned:
                ev.kind = event_kind::number_unsigned;
                ev.unsigned_value = m_lexer.get_number_unsigned();
                ev.raw = m_lexer.get_token_string();
                break;
            case token_type::value_float:
                ev.kind = event_kind::number_float;
                ev.float_value = m_lexer.get_number_float();
                ev.raw = m_lexer.get_token_string();
                break;
            case token_type::value_string:
                ev.kind = event_kind::string_value;
                if (!m_discard)
                {
                    ev.string_value = std::move(m_lexer.get_string());
                }
                break;
            case token_type::begin_object:
                ev.kind = event_kind::begin_object;
                m_stack.push_back(frame {true, state::object_key_or_end});
                return true; // the container value completes with its end event
            case token_type::begin_array:
                ev.kind = event_kind::begin_array;
                m_stack.push_back(frame {false, state::array_value_or_end});
                return true;
            default:
                return false;
        }

        // a scalar value completed inside the current context
        value_completed();
        return true;
    }

    /// produce an end_object/end_array event and update the parent context
    event close_container(const event_kind kind, const position_t& pos)
    {
        m_stack.pop_back();
        value_completed();
        event ev;
        ev.kind = kind;
        ev.position = pos;
        return ev;
    }

    /// mark the innermost context as having completed a value
    void value_completed() noexcept
    {
        if (m_stack.empty())
        {
            m_root_done = true;
            return;
        }
        frame& f = m_stack.back();
        if (f.is_object)
        {
            f.state_ = state::object_comma_or_end;
        }
        else
        {
            f.state_ = state::array_comma_or_end;
        }
    }

    nlohmann::detail::parse_error unexpected(const token_type tok, const char* expected) const
    {
        return nlohmann::detail::parse_error::create(101, m_lexer.get_position(),
                nlohmann::detail::concat("syntax error - unexpected ", lexer_t::token_type_name(tok),
                                         "; expected ", expected), nullptr);
    }

    lexer_t m_lexer;
    std::vector<frame> m_stack {};
    bool m_root_done = false;
    bool m_discard = false;
};

/// one segment of the current nesting path
struct path_frame
{
    path_frame(const bool is_object_, std::string key_, const std::size_t index_)
        : is_object(is_object_), key(std::move(key_)), index(index_)
    {}

    bool is_object;
    std::string key;      ///< current key (object frames)
    std::size_t index;    ///< current element index (array frames)
};

/// escape one JSON Pointer segment (RFC 6901)
inline std::string escape_pointer_segment(const std::string& s)
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

/// render the current nesting path as an RFC 6901 JSON Pointer
inline std::string render_pointer(const std::vector<path_frame>& path)
{
    std::string out;
    for (const auto& f : path)
    {
        out += '/';
        out += f.is_object ? escape_pointer_segment(f.key) : std::to_string(f.index);
    }
    return out;
}

/// whether the event can start a JSON value
inline bool is_value_start(const event_kind k) noexcept
{
    switch (k)
    {
        case event_kind::null_value:
        case event_kind::boolean:
        case event_kind::number_integer:
        case event_kind::number_unsigned:
        case event_kind::number_float:
        case event_kind::string_value:
        case event_kind::begin_object:
        case event_kind::begin_array:
            return true;
        default:
            return false;
    }
}

/// whether the event is a number of any of the library's three number types
inline bool is_number(const event_kind k) noexcept
{
    return k == event_kind::number_integer
           || k == event_kind::number_unsigned
           || k == event_kind::number_float;
}

/// compare two number events with the same rules as nlohmann::json::operator==
template<typename BasicJsonType, typename Event>
bool numbers_equal(const Event& a, const Event& b)
{
    using number_unsigned_t = typename BasicJsonType::number_unsigned_t;
    using number_float_t = typename BasicJsonType::number_float_t;

    if (a.kind == b.kind)
    {
        switch (a.kind)
        {
            case event_kind::number_integer:
                return a.integer_value == b.integer_value;
            case event_kind::number_unsigned:
                return a.unsigned_value == b.unsigned_value;
            default:
                return a.float_value == b.float_value;
        }
    }

    // mixed types: mirror JSON_IMPLEMENT_OPERATOR(==) of basic_json
    if (a.kind == event_kind::number_float || b.kind == event_kind::number_float)
    {
        const Event& f = a.kind == event_kind::number_float ? a : b;
        const Event& i = a.kind == event_kind::number_float ? b : a;
        if (i.kind == event_kind::number_integer)
        {
            return nlohmann::detail::compare_integer_with_float(i.integer_value, f.float_value) == static_cast<number_float_t>(0);
        }
        return nlohmann::detail::compare_integer_with_float(i.unsigned_value, f.float_value) == static_cast<number_float_t>(0);
    }

    // integer vs. unsigned
    const auto i = a.kind == event_kind::number_integer ? a.integer_value : b.integer_value;
    const auto u = a.kind == event_kind::number_unsigned ? a.unsigned_value : b.unsigned_value;
    return i >= 0 && static_cast<number_unsigned_t>(i) == u;
}

/// build the bounded token preview for the report
inline token_info make_token_info(const std::string& preview, const nlohmann::detail::position_t& pos,
                                  const std::size_t max_preview_length)
{
    token_info info;
    info.position.offset = pos.chars_read_total;
    info.position.line = pos.lines_read + 1;
    info.position.column = pos.chars_read_current_line;
    info.preview = preview;
    if (info.preview.size() > max_preview_length)
    {
        info.preview.resize(max_preview_length);
        info.preview += "...";
        info.truncated = true;
    }
    return info;
}

template<typename Event>
token_info make_token_info(const Event& ev, const std::size_t max_preview_length)
{
    const std::string quote(1, static_cast<char>(0x22)); // double quote
    std::string preview;
    switch (ev.kind)
    {
        case event_kind::null_value: preview = "null"; break;
        case event_kind::boolean: preview = ev.boolean_value ? "true" : "false"; break;
        case event_kind::number_integer:
        case event_kind::number_unsigned:
        case event_kind::number_float: preview = ev.raw; break;
        case event_kind::string_value: preview = quote + ev.string_value + quote; break;
        case event_kind::key: preview = "key " + quote + ev.string_value + quote; break;
        case event_kind::begin_object: preview = "{"; break;
        case event_kind::end_object: preview = "}"; break;
        case event_kind::begin_array: preview = "["; break;
        case event_kind::end_array: preview = "]"; break;
        case event_kind::end_of_input: preview = "<end of input>"; break;
    }
    return make_token_info(preview, ev.position, max_preview_length);
}

} // namespace detail

/*!
@brief streaming comparer for a given basic_json type

@tparam BasicJsonType  the basic_json specialization whose number and string
types (and hence whose parsing and equality rules) should be used
*/
template<typename BasicJsonType>
class basic_comparer
{
  public:
    /*!
    @brief compare two JSON inputs token by token without building a DOM

    The inputs accept everything nlohmann::json::parse accepts: strings,
    string literals, character/contiguous containers, iterator pairs,
    std::istream, and std::FILE*.
    */
    template<typename InputA, typename InputB>
    static result compare(InputA&& a, InputB&& b, const options& opt = options())
    {
        return run(nlohmann::detail::input_adapter(std::forward<InputA>(a)),
                   nlohmann::detail::input_adapter(std::forward<InputB>(b)), opt);
    }

  private:
    template<typename AdapterA, typename AdapterB>
    static result run(AdapterA&& adapter_a, AdapterB&& adapter_b, const options& opt)
    {
        using stream_a = detail::event_stream<BasicJsonType, AdapterA>;
        using stream_b = detail::event_stream<BasicJsonType, AdapterB>;
        using event = typename stream_a::event;
        using event_kind = detail::event_kind;

        stream_a sa(std::move(adapter_a));
        stream_b sb(std::move(adapter_b));

        result res;
        std::vector<detail::path_frame> path;
        bool recorded = false; // whether an outcome (difference or depth error) was recorded

        for (;;)
        {
            event ea;
            event eb;
            try
            {
                ea = sa.next();
            }
            catch (const nlohmann::detail::parse_error& e)
            {
                return syntax_error_result(1, e, sa.current_position(), opt);
            }
            try
            {
                eb = sb.next();
            }
            catch (const nlohmann::detail::parse_error& e)
            {
                return syntax_error_result(2, e, sb.current_position(), opt);
            }

            if (recorded)
            {
                // validate-only mode: consume both inputs to the end so that
                // syntax errors are still detected (they take priority)
                if (ea.kind == event_kind::end_of_input && eb.kind == event_kind::end_of_input)
                {
                    return res;
                }
                continue;
            }

            if (ea.kind == event_kind::end_of_input && eb.kind == event_kind::end_of_input)
            {
                return res; // equal
            }

            // keep the nesting path and the depth limit up to date
            if (ea.kind == event_kind::begin_object || ea.kind == event_kind::begin_array)
            {
                if (ea.kind == eb.kind)
                {
                    path.push_back(detail::path_frame {ea.kind == event_kind::begin_object, "", 0});
                    if (opt.max_depth != 0 && path.size() > opt.max_depth)
                    {
                        record_depth_exceeded(res, path, opt);
                        recorded = true;
                        sa.discard_values();
                        sb.discard_values();
                    }
                    continue;
                }
            }
            else if (ea.kind == event_kind::end_object || ea.kind == event_kind::end_array)
            {
                if (ea.kind == eb.kind)
                {
                    if (!path.empty())
                    {
                        path.pop_back();
                    }
                    advance_parent(path);
                    continue;
                }
            }
            else if (ea.kind == event_kind::key && eb.kind == event_kind::key)
            {
                if (ea.string_value == eb.string_value)
                {
                    path.back().key = ea.string_value;
                    continue;
                }
            }
            else if ((ea.kind == eb.kind
                      || (detail::is_number(ea.kind) && detail::is_number(eb.kind)))
                     && equal_scalar(ea, eb))
            {
                advance_parent(path);
                continue;
            }

            // first difference found: record it and switch to validate-only mode
            record_difference(res, ea, eb, path, opt);
            recorded = true;
            sa.discard_values();
            sb.discard_values();
        }
    }

    /// whether two events of the same kind carry equal values
    template<typename Event>
    static bool equal_scalar(const Event& a, const Event& b)
    {
        using event_kind = detail::event_kind;
        switch (a.kind)
        {
            case event_kind::null_value:
                return true;
            case event_kind::boolean:
                return a.boolean_value == b.boolean_value;
            case event_kind::number_integer:
            case event_kind::number_unsigned:
            case event_kind::number_float:
                return detail::numbers_equal<BasicJsonType>(a, b);
            case event_kind::string_value:
                return a.string_value == b.string_value;
            default:
                return false;
        }
    }

    /// move to the next array element after a completed value
    static void advance_parent(std::vector<detail::path_frame>& path)
    {
        if (!path.empty() && !path.back().is_object)
        {
            ++path.back().index;
        }
    }

    static result syntax_error_result(const int side, const nlohmann::detail::parse_error& e,
                                      const nlohmann::detail::position_t& pos, const options& opt)
    {
        result res;
        res.outcome = status::syntax_error;
        res.error_side = side;
        const token_info info = detail::make_token_info("<syntax error>", pos, opt.max_preview_length);
        if (side == 1)
        {
            res.lhs = info;
        }
        else
        {
            res.rhs = info;
        }
        res.message = nlohmann::detail::concat("syntax error in input ", std::to_string(side), ": ", e.what());
        return res;
    }

    static void record_depth_exceeded(result& res, const std::vector<detail::path_frame>& path,
                                      const options& opt)
    {
        res.outcome = status::depth_exceeded;
        res.json_pointer = detail::render_pointer(path);
        res.message = nlohmann::detail::concat("maximum nesting depth ", std::to_string(opt.max_depth),
                             " exceeded at ", res.json_pointer.empty() ? std::string("<root>") : res.json_pointer);
    }

    template<typename Event>
    static void record_difference(result& res, const Event& ea, const Event& eb,
                                  std::vector<detail::path_frame>& path, const options& opt)
    {
        using event_kind = detail::event_kind;

        res.outcome = status::different;
        res.lhs = detail::make_token_info(ea, opt.max_preview_length);
        res.rhs = detail::make_token_info(eb, opt.max_preview_length);

        const bool a_value = detail::is_value_start(ea.kind);
        const bool b_value = detail::is_value_start(eb.kind);

        if (ea.kind == event_kind::key && eb.kind == event_kind::key)
        {
            // member order is significant: same position, different key
            res.kind = diff_kind::key_mismatch;
            path.back().key = ea.string_value;
            res.json_pointer = detail::render_pointer(path);
        }
        else if (ea.kind == event_kind::key || eb.kind == event_kind::key)
        {
            // one object still has members while the other ended
            res.kind = diff_kind::object_size_mismatch;
            const Event& extra = ea.kind == event_kind::key ? ea : eb;
            path.back().key = extra.string_value;
            res.json_pointer = detail::render_pointer(path);
        }
        else if ((ea.kind == event_kind::end_array && b_value) || (eb.kind == event_kind::end_array && a_value))
        {
            // one array still has elements while the other ended
            res.kind = diff_kind::array_size_mismatch;
            res.json_pointer = detail::render_pointer(path);
        }
        else if (ea.kind == event_kind::end_object || eb.kind == event_kind::end_object)
        {
            res.kind = diff_kind::object_size_mismatch;
            res.json_pointer = detail::render_pointer(path);
        }
        else if (ea.kind != eb.kind && detail::is_number(ea.kind) && detail::is_number(eb.kind))
        {
            // different number types are still compared by value (library rules)
            res.kind = diff_kind::value_mismatch;
            res.json_pointer = detail::render_pointer(path);
        }
        else if (ea.kind != eb.kind)
        {
            res.kind = diff_kind::type_mismatch;
            res.json_pointer = detail::render_pointer(path);
        }
        else
        {
            // same kind, different value (boolean, number, string)
            res.kind = diff_kind::value_mismatch;
            res.json_pointer = detail::render_pointer(path);
        }

        res.message = nlohmann::detail::concat("first difference at ",
                             res.json_pointer.empty() ? std::string("<root>") : res.json_pointer,
                             ": ", to_string(res.kind));
    }
};

/// streaming comparer using nlohmann::json's value types
using comparer = basic_comparer<json>;

/*!
@brief compare two JSON inputs token by token without building a DOM

@sa basic_comparer::compare
*/
template<typename InputA, typename InputB>
result compare(InputA&& a, InputB&& b, const options& opt = options())
{
    return comparer::compare(std::forward<InputA>(a), std::forward<InputB>(b), opt);
}

} // namespace json_diff

NLOHMANN_JSON_NAMESPACE_END

#undef JSON_DIFF_LIKELY
#undef JSON_DIFF_UNLIKELY
