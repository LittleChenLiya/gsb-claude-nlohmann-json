//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic> // atomic
#include <cstddef> // size_t
#include <cstdio> // remove, rename
#include <fstream> // ofstream
#include <istream> // istream
#include <map> // map
#include <ostream> // ostream
#include <string> // string, to_string
#include <utility> // move
#include <vector> // vector

#include <nlohmann/json.hpp>

// <nlohmann/json.hpp> undefines its internal macros at the end, so this
// header brings its own private variants (undef'd again below)
#if (defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)) && !defined(JSON_NOEXCEPTION)
    #if defined(JSON_THROW_USER)
        #define NLOHMANN_REDACTOR_THROW(exception) JSON_THROW_USER(exception)
    #else
        #define NLOHMANN_REDACTOR_THROW(exception) throw exception
    #endif
    #define NLOHMANN_REDACTOR_TRY try
    #define NLOHMANN_REDACTOR_CATCH_ALL catch (...)
    #define NLOHMANN_REDACTOR_RETHROW throw
#else
    #include <cstdlib> // abort
    #define NLOHMANN_REDACTOR_THROW(exception) std::abort()
    #define NLOHMANN_REDACTOR_TRY if (true)
    #define NLOHMANN_REDACTOR_CATCH_ALL if (false)
    #define NLOHMANN_REDACTOR_RETHROW std::abort()
#endif

NLOHMANN_JSON_NAMESPACE_BEGIN
namespace detail
{

/// output adapter that turns std::ostream failures into exceptions so a
/// staging sink never reports success for a partially written document
class checked_ostream_output_adapter : public output_adapter_protocol<char>
{
  public:
    explicit checked_ostream_output_adapter(std::ostream& stream) noexcept
        : stream(stream)
    {}

    void write_character(char c) override
    {
        stream.put(c);
        check();
    }

    void write_characters(const char* s, std::size_t length) override
    {
        stream.write(s, static_cast<std::streamsize>(length));
        check();
    }

  private:
    void check() const
    {
        if (!stream.good())
        {
            NLOHMANN_REDACTOR_THROW(other_error::create(651, "redaction sink write failed", nullptr));
        }
    }

    std::ostream& stream;
};

/*!
@brief SAX handler that transcodes JSON text while replacing matched subtrees

The handler receives every parser event (so the whole input, including
replaced subtrees, is syntax-checked by the parser) but only serializes the
values that are not covered by a redaction rule. Matched subtrees are
swallowed with a depth counter: no DOM is built for them and no further
rule lookup happens inside them (a matching parent rule suppresses nested
rules). Array indices are counted on the original input, so removing an
element from the output does not shift the indices of its siblings.
*/
template<typename BasicJsonType>
class redaction_sax
{
    using string_t = typename BasicJsonType::string_t;
    using number_integer_t = typename BasicJsonType::number_integer_t;
    using number_unsigned_t = typename BasicJsonType::number_unsigned_t;
    using number_float_t = typename BasicJsonType::number_float_t;
    using binary_t = typename BasicJsonType::binary_t;

  public:
    using rule_map = std::map<json_pointer<string_t>, std::string>;

    redaction_sax(const rule_map& rules, output_adapter_t<char> out)
        : rules(rules), out(std::move(out))
    {}

    bool null()
    {
        return scalar("null");
    }

    bool boolean(bool val)
    {
        return scalar(val ? "true" : "false");
    }

    bool number_integer(number_integer_t val)
    {
        return scalar(std::to_string(val));
    }

    bool number_unsigned(number_unsigned_t val)
    {
        return scalar(std::to_string(val));
    }

    bool number_float(number_float_t /*val*/, const string_t& token)
    {
        // re-emit the raw token so the precision of unmatched numbers is
        // preserved exactly as read by the parser
        return scalar(token);
    }

    bool string(string_t& val)
    {
        return scalar(quote(val));
    }

    bool binary(binary_t& /*val*/)
    {
        // JSON text input never produces binary values
        return false;
    }

    bool key(string_t& val)
    {
        if (skipping())
        {
            return true;
        }
        pending_key = val;
        return true;
    }

    bool start_object(std::size_t /*elements*/)
    {
        return start_container('{', true);
    }

    bool end_object()
    {
        return end_container('}');
    }

    bool start_array(std::size_t /*elements*/)
    {
        return start_container('[', false);
    }

    bool end_array()
    {
        return end_container(']');
    }

    template<class Exception>
    bool parse_error(std::size_t /*position*/, const std::string& /*last_token*/,
                     const Exception& ex)
    {
        static_cast<void>(ex); // unused when exceptions are disabled
        NLOHMANN_REDACTOR_THROW(ex);
        return false;
    }

  private:
    struct frame
    {
        explicit frame(bool object) noexcept
            : is_object(object)
        {}

        bool is_object;        // whether this container is an object
        std::size_t count = 0; // values already seen (separator bookkeeping)
        std::size_t index = 0; // next array index in original input order
    };

    bool skipping() const noexcept
    {
        return skip_depth > 0;
    }

    void write(char c)
    {
        out->write_character(c);
    }

    void write(const std::string& text)
    {
        out->write_characters(text.data(), text.size());
    }

    std::string quote(const string_t& value) const
    {
        return BasicJsonType(value).dump();
    }

    /*!
    @brief emit separators/key for the next value and look up its rule
    @return pointer to the replacement text if the value matches a rule
    */
    const std::string* begin_value()
    {
        if (!stack.empty())
        {
            frame& top = stack.back();
            if (top.count > 0)
            {
                write(',');
            }
            if (top.is_object)
            {
                current.push_back(pending_key);
                write(quote(pending_key));
                write(':');
            }
            else
            {
                current.push_back(std::to_string(top.index));
                ++top.index;
            }
            ++top.count;
        }
        const auto it = rules.find(current);
        return it != rules.end() ? &it->second : nullptr;
    }

    bool scalar(const std::string& text)
    {
        if (skipping())
        {
            return true;
        }
        const std::string* replacement = begin_value();
        write(replacement != nullptr ? *replacement : text);
        if (!stack.empty())
        {
            current.pop_back();
        }
        return true;
    }

    bool start_container(char open, bool is_object)
    {
        if (skipping())
        {
            ++skip_depth;
            return true;
        }
        const std::string* replacement = begin_value();
        if (replacement != nullptr)
        {
            // swallow the whole subtree: the parser keeps syntax-checking
            // it, but nothing is built and no nested rule fires
            write(*replacement);
            skip_depth = 1;
            return true;
        }
        write(open);
        stack.emplace_back(is_object);
        return true;
    }

    bool end_container(char close)
    {
        if (skipping())
        {
            --skip_depth;
            if (skip_depth == 0 && !stack.empty())
            {
                current.pop_back();
            }
            return true;
        }
        write(close);
        stack.pop_back();
        if (!stack.empty())
        {
            current.pop_back();
        }
        return true;
    }

    const rule_map& rules;                 // exact pointer -> replacement text
    output_adapter_t<char> out;            // staging sink
    json_pointer<string_t> current;        // pointer of the current container
    string_t pending_key;                  // last object key seen
    std::vector<frame> stack;              // open containers
    std::size_t skip_depth = 0;            // depth of a swallowed subtree
};

}  // namespace detail

/*!
@brief a single redaction rule for @ref json_redactor

The pointer is an exact RFC 6901 JSON Pointer ("" addresses the whole
document, "~0"/"~1" escape "~" and "/") and the replacement is a serialized
JSON value that is spliced into the output at the matched position.
*/
struct redaction_rule
{
    std::string pointer;     // exact JSON Pointer of the subtree to replace
    std::string replacement; // serialized JSON value used as replacement
};

/*!
@brief streaming redaction transcoder

Reads JSON text with the SAX parser and writes a complete, valid JSON
document in which only the subtrees selected by exact JSON Pointer rules
are replaced. Unmatched values are forwarded as parsed (number precision is
preserved according to the parser configuration; floating-point tokens are
re-emitted verbatim). The output is written to a staging sink; only
@ref transcode_file publishes a result file, and only after the whole input
was processed successfully, so a corrupted input never publishes a partial
document.

Complexity: O(n) time in the input size (plus O(log r) rule lookup per
value for r rules), O(d + r) memory for nesting depth d - independent of
the document size. Replaced subtrees are validated but neither stored nor
reported to user callbacks.
*/
class json_redactor
{
  public:
    json_redactor() = default;

    explicit json_redactor(std::vector<redaction_rule> rules)
    {
        for (auto& rule : rules)
        {
            add_rule(std::move(rule));
        }
    }

    /*!
    @brief add a rule; a later rule with the same pointer replaces an earlier one
    @throw parse_error if the pointer syntax is invalid
    @throw other_error.652 if the replacement is not valid JSON
    */
    void add_rule(redaction_rule rule)
    {
        json_pointer<std::string> pointer(rule.pointer);
        if (!json::accept(rule.replacement))
        {
            NLOHMANN_REDACTOR_THROW(detail::other_error::create(652,
                                    "redaction replacement for '" + rule.pointer + "' is not valid JSON", nullptr));
        }
        rules[std::move(pointer)] = std::move(rule.replacement);
    }

    /*!
    @brief transcode JSON text from @a input into the staging sink @a sink
    @throw parse_error if the input is not valid JSON (also inside replaced
           subtrees, which are still fully syntax-checked)
    @throw other_error.651 if writing to the sink fails
    */
    void transcode(std::istream& input, std::ostream& sink) const
    {
        detail::output_adapter_t<char> adapter(new detail::checked_ostream_output_adapter(sink));
        detail::redaction_sax<json> sax(rules, adapter);
        if (!json::sax_parse(input, &sax))
        {
            NLOHMANN_REDACTOR_THROW(detail::other_error::create(653, "redaction failed", nullptr));
        }
        sink.flush();
        if (!sink.good())
        {
            NLOHMANN_REDACTOR_THROW(detail::other_error::create(651, "redaction sink write failed", nullptr));
        }
    }

    /*!
    @brief transcode JSON text from @a input and publish it to @a output_path

    The output is staged in a temporary file next to @a output_path and only
    renamed to the final name after the whole input was processed
    successfully. If the input is corrupted or the sink fails, the staging
    file is removed and @a output_path is left untouched - no partial
    document is ever published. The output is never buffered in memory.

    @throw parse_error if the input is not valid JSON
    @throw other_error.651 if writing to the staging file fails
    @throw other_error.654 if the staging file cannot be opened
    @throw other_error.655 if the staging file cannot be published
    */
    void transcode_file(std::istream& input, const std::string& output_path) const
    {
        const std::string staging_path = make_staging_path(output_path);
        std::ofstream staging(staging_path.c_str(), std::ios::binary | std::ios::trunc);
        if (!staging.is_open())
        {
            NLOHMANN_REDACTOR_THROW(detail::other_error::create(654,
                                    "cannot open redaction staging file '" + staging_path + "'", nullptr));
        }
        NLOHMANN_REDACTOR_TRY
        {
            transcode(input, staging);
            staging.flush();
            staging.close();
            if (staging.fail())
            {
                NLOHMANN_REDACTOR_THROW(detail::other_error::create(651, "redaction sink write failed", nullptr));
            }
        }
        NLOHMANN_REDACTOR_CATCH_ALL
        {
            staging.close();
            std::remove(staging_path.c_str());
            NLOHMANN_REDACTOR_RETHROW;
        }
        publish(staging_path, output_path);
    }

  private:
    static void publish(const std::string& staging_path, const std::string& output_path)
    {
        if (std::rename(staging_path.c_str(), output_path.c_str()) != 0)
        {
            // POSIX rename() atomically replaces the target; some platforms
            // (e.g. Windows) refuse to overwrite, so retry after removal
            std::remove(output_path.c_str());
            if (std::rename(staging_path.c_str(), output_path.c_str()) != 0)
            {
                std::remove(staging_path.c_str());
                NLOHMANN_REDACTOR_THROW(detail::other_error::create(655,
                                        "cannot publish redacted document to '" + output_path + "'", nullptr));
            }
        }
    }

    static std::string make_staging_path(const std::string& output_path)
    {
        static std::atomic<unsigned long> counter{0}; // NOLINT(cert-err58-cpp)
        return output_path + ".staging-" + std::to_string(++counter);
    }

    std::map<json_pointer<std::string>, std::string> rules;
};

NLOHMANN_JSON_NAMESPACE_END

#undef NLOHMANN_REDACTOR_THROW
#undef NLOHMANN_REDACTOR_TRY
#undef NLOHMANN_REDACTOR_CATCH_ALL
#undef NLOHMANN_REDACTOR_RETHROW
