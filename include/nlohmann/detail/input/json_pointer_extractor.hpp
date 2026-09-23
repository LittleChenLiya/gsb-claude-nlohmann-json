//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef> // size_t
#include <limits> // numeric_limits
#include <map> // map
#include <memory> // unique_ptr
#include <set> // set
#include <string> // string
#include <utility> // forward, move
#include <vector> // vector

#include <nlohmann/detail/exceptions.hpp>
#include <nlohmann/detail/input/input_adapters.hpp>
#include <nlohmann/detail/input/json_sax.hpp>
#include <nlohmann/detail/json_pointer.hpp>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/string_concat.hpp>
#include <nlohmann/detail/string_escape.hpp>
#include <nlohmann/json_fwd.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

/*!
@brief resource limits for a single subtree extracted by @ref sax_extract

The byte budget is an approximation of the serialized size of the matched
subtree: string, key, and binary values contribute their length, numbers
contribute 8 bytes, and every other value or structural token contributes
1 byte. The depth budget counts containers opened below the root of the
matched subtree (a scalar match has depth 0).
*/
struct sax_extract_limits
{
    std::size_t max_subtree_bytes = (std::numeric_limits<std::size_t>::max)(); ///< per-subtree byte budget
    std::size_t max_subtree_depth = (std::numeric_limits<std::size_t>::max)(); ///< per-subtree container depth budget
};

/*!
@brief summary of a @ref sax_extract run

@a complete is true only if the whole input was consumed, is syntactically
valid JSON, no limit was hit, and the callback never stopped the extraction.
Pointers that never matched are reported together in @a unmatched once the
input is exhausted.
*/
template<typename StringType = std::string>
struct sax_extract_result
{
    std::size_t emitted = 0;               ///< number of subtrees delivered to the callback
    bool complete = false;                 ///< whole document parsed successfully and not stopped early
    bool stopped_by_callback = false;      ///< extraction stopped because the callback returned false
    bool limit_exceeded = false;           ///< extraction stopped because a subtree limit was exceeded
    bool parse_failed = false;             ///< input is not valid JSON (or a resource limit of the parser was hit)
    std::string error;                     ///< details when the extraction did not complete
    std::vector<StringType> unmatched;     ///< requested pointers that never matched, in request order
};

namespace detail
{

/*!
@brief SAX acceptor extracting multiple JSON Pointer addressed subtrees

The requested pointers are compiled into a shared-prefix trie. While the
parser streams the document, only values located at a requested pointer are
materialized into BasicJsonType values (via json_sax_dom_parser); all other
events are consumed for full syntax validation but never stored. Memory
usage is bounded by the trie, the currently open (nested) matched subtrees,
and per-object bookkeeping of relevant keys only -- never by the size of
unselected parts of the document.
*/
template<typename BasicJsonType, typename Callback>
class json_multi_pointer_extractor
{
    using string_t = typename BasicJsonType::string_t;
    using binary_t = typename BasicJsonType::binary_t;
    using number_integer_t = typename BasicJsonType::number_integer_t;
    using number_unsigned_t = typename BasicJsonType::number_unsigned_t;
    using number_float_t = typename BasicJsonType::number_float_t;

    static constexpr std::size_t no_request = (std::numeric_limits<std::size_t>::max)();

    struct trie_node
    {
        std::map<string_t, std::unique_ptr<trie_node>> children;
        std::size_t request = no_request; // index into requests when a pointer ends here
    };

    struct frame
    {
        const trie_node* node = nullptr;  // trie node of this container, nullptr when off any requested path
        bool is_object = false;
        std::size_t next_index = 0;       // next array index (arrays only)
        string_t key;                     // pending object key (only when node != nullptr)
        string_t path;                    // JSON pointer of this container, for diagnostics (only when node != nullptr)
        std::set<string_t> seen;          // relevant keys already consumed in this object
    };

    struct capture
    {
        explicit capture(std::size_t request_)
            : request(request_)
        {}

        std::size_t request;
        std::size_t depth = 0;            // containers opened inside the captured subtree
        std::size_t bytes = 0;            // approximate serialized size of the captured subtree
        BasicJsonType value;
        json_sax_dom_parser<BasicJsonType, span_input_adapter> dom{value};
    };

  public:
    json_multi_pointer_extractor(const std::vector<string_t>& pointers, Callback& callback_, const sax_extract_limits& limits_)
        : callback(callback_), limits(limits_)
    {
        for (const auto& pointer_string : pointers)
        {
            // reuse json_pointer for splitting and unescaping ('~0' / '~1'),
            // so the exact same escaping rules and validation apply
            json_pointer<string_t> ptr(pointer_string);
            std::vector<string_t> tokens;
            while (!ptr.empty())
            {
                tokens.push_back(ptr.back());
                ptr.pop_back();
            }

            trie_node* node = &root;
            for (auto it = tokens.rbegin(); it != tokens.rend(); ++it)
            {
                auto& child = node->children[*it];
                if (child == nullptr)
                {
                    child.reset(new trie_node());
                }
                node = child.get();
            }

            // duplicate requests are delivered only once
            if (node->request == no_request)
            {
                node->request = requests.size();
                requests.push_back(pointer_string);
            }
        }
        matched.assign(requests.size(), false);
    }

    json_multi_pointer_extractor(const json_multi_pointer_extractor&) = delete;
    json_multi_pointer_extractor& operator=(const json_multi_pointer_extractor&) = delete;

    template<typename InputType>
    sax_extract_result<string_t> run(InputType&& input)
    {
        try
        {
            BasicJsonType::sax_parse(std::forward<InputType>(input), this);
        }
        catch (const detail::exception& ex)
        {
            if (duplicate_rejected)
            {
                throw; // ambiguous requested paths are rejected with an exception
            }
            res.parse_failed = true;
            res.error = ex.what();
        }

        res.complete = !res.parse_failed && !res.limit_exceeded && !res.stopped_by_callback;

        // report all pointers that never matched now that the input is exhausted
        for (std::size_t i = 0; i < requests.size(); ++i)
        {
            if (!matched[i])
            {
                res.unmatched.push_back(requests[i]);
            }
        }
        return std::move(res);
    }

    bool null()
    {
        before_value();
        return forward_scalar(4, [](capture & c)
        {
            return c.dom.null();
        });
    }

    bool boolean(bool val)
    {
        before_value();
        return forward_scalar(1, [val](capture & c)
        {
            return c.dom.boolean(val);
        });
    }

    bool number_integer(number_integer_t val)
    {
        before_value();
        return forward_scalar(8, [val](capture & c)
        {
            return c.dom.number_integer(val);
        });
    }

    bool number_unsigned(number_unsigned_t val)
    {
        before_value();
        return forward_scalar(8, [val](capture & c)
        {
            return c.dom.number_unsigned(val);
        });
    }

    bool number_float(number_float_t val, const string_t& /*str*/)
    {
        before_value();
        return forward_scalar(8, [val](capture & c)
        {
            return c.dom.number_float(val, "");
        });
    }

    bool string(string_t& val)
    {
        before_value();
        return forward_string_like(val, [&val](capture & c, string_t& v)
        {
            return c.dom.string(v);
        });
    }

    bool binary(binary_t& val)
    {
        before_value();
        return forward_string_like(val, [&val](capture & c, binary_t& v)
        {
            return c.dom.binary(v);
        });
    }

    bool key(string_t& val)
    {
        frame& f = frames.back();
        if (f.node != nullptr)
        {
            // only duplicates that make a requested path ambiguous are rejected;
            // duplicates elsewhere keep the original parser behavior
            if (f.node->children.find(val) != f.node->children.end() && !f.seen.insert(val).second)
            {
                duplicate_rejected = true;
                JSON_THROW(detail::parse_error::create(112, 0, detail::concat("duplicate object member '", val, "' at JSON pointer '", f.path, "' makes a requested extraction path ambiguous"), nullptr));
            }
            f.key = val;
        }
        return forward_string_like(val, [&val](capture & c, string_t& v)
        {
            return c.dom.key(v);
        });
    }

    bool start_object(std::size_t len)
    {
        before_value();
        push_frame(true);
        return forward_start_container(len, true);
    }

    bool end_object()
    {
        return end_container(true);
    }

    bool start_array(std::size_t len)
    {
        before_value();
        push_frame(false);
        return forward_start_container(len, false);
    }

    bool end_array()
    {
        return end_container(false);
    }

    bool parse_error(std::size_t /*position*/, const std::string& /*last_token*/, const detail::exception& ex)
    {
        res.parse_failed = true;
        res.error = ex.what();
        return false;
    }

  private:
    static string_t index_token(std::size_t idx)
    {
        const std::string s = std::to_string(idx);
        return string_t(s.begin(), s.end()); // digits are ASCII and widen safely
    }

    // trie matching for the value that is about to be reported; starts a
    // capture when the value sits exactly at a requested pointer
    void before_value()
    {
        const trie_node* node = nullptr;
        current_path.clear();

        if (frames.empty())
        {
            node = &root;
        }
        else
        {
            frame& f = frames.back();
            if (f.node != nullptr)
            {
                string_t token;
                if (f.is_object)
                {
                    token = f.key;
                }
                else
                {
                    token = index_token(f.next_index);
                    ++f.next_index;
                }

                const auto it = f.node->children.find(token);
                if (it != f.node->children.end())
                {
                    node = it->second.get();
                }

                current_path = f.path;
                current_path += '/';
                current_path += detail::escape(token);
            }
        }

        current_node = node;
        if (node != nullptr && node->request != no_request)
        {
            captures.emplace_back(new capture(node->request));
        }
    }

    void push_frame(bool is_object)
    {
        frames.emplace_back();
        frame& f = frames.back();
        f.node = current_node;
        f.is_object = is_object;
        if (f.node != nullptr)
        {
            f.path = current_path;
        }
    }

    // account bytes for every active capture and stop when a budget is exceeded
    bool account(std::size_t bytes)
    {
        for (const auto& c : captures)
        {
            c->bytes += bytes;
            if (JSON_HEDLEY_UNLIKELY(c->bytes > limits.max_subtree_bytes))
            {
                res.limit_exceeded = true;
                res.error = "byte budget of an extracted subtree exceeded";
                return false;
            }
        }
        return true;
    }

    // forward a string-like event; the value is copied for all but the last
    // capture because json_sax_dom_parser moves from the passed value
    template<typename T, typename F>
    bool forward_string_like(T& val, F&& fn)
    {
        if (!account(val.size()))
        {
            return false;
        }
        for (std::size_t i = 0; i < captures.size(); ++i)
        {
            if (i + 1 < captures.size())
            {
                T copy = val;
                if (!fn(*captures[i], copy))
                {
                    return false;
                }
            }
            else if (!fn(*captures[i], val))
            {
                return false;
            }
        }
        return after_value();
    }

    template<typename F>
    bool forward_scalar(std::size_t bytes, F&& fn)
    {
        if (!account(bytes))
        {
            return false;
        }
        for (const auto& c : captures)
        {
            if (!fn(*c))
            {
                return false;
            }
        }
        return after_value();
    }

    bool forward_start_container(std::size_t len, bool is_object)
    {
        if (!account(1))
        {
            return false;
        }
        for (const auto& c : captures)
        {
            ++c->depth;
            if (JSON_HEDLEY_UNLIKELY(c->depth > limits.max_subtree_depth))
            {
                res.limit_exceeded = true;
                res.error = "depth budget of an extracted subtree exceeded";
                return false;
            }
            if (is_object)
            {
                c->dom.start_object(len);
            }
            else
            {
                c->dom.start_array(len);
            }
        }
        return true;
    }

    bool end_container(bool is_object)
    {
        if (!account(1))
        {
            return false;
        }
        for (const auto& c : captures)
        {
            if (is_object)
            {
                c->dom.end_object();
            }
            else
            {
                c->dom.end_array();
            }
            --c->depth;
        }
        frames.pop_back();
        return after_value();
    }

    // a capture whose depth returned to zero just saw its root value complete;
    // nested captures always complete before their enclosing ones, so only the
    // innermost (last) capture can be done here
    bool after_value()
    {
        if (!captures.empty() && captures.back()->depth == 0)
        {
            return complete_capture();
        }
        return true;
    }

    bool complete_capture()
    {
        std::unique_ptr<capture> c = std::move(captures.back());
        captures.pop_back();
        matched[c->request] = true;
        ++res.emitted;
        if (!callback(requests[c->request], std::move(c->value)))
        {
            res.stopped_by_callback = true;
            return false;
        }
        return true;
    }

    std::vector<string_t> requests;          // deduplicated requested pointers, in request order
    std::vector<bool> matched;               // whether a request already matched
    trie_node root;                          // shared-prefix trie of all requested pointers
    std::vector<frame> frames;               // container nesting
    std::vector<std::unique_ptr<capture>> captures; // currently open (nested) matched subtrees
    const trie_node* current_node = nullptr; // trie node of the value being reported
    string_t current_path;                   // JSON pointer of the value being reported
    Callback& callback;
    sax_extract_limits limits;
    sax_extract_result<string_t> res;
    bool duplicate_rejected = false;
};

} // namespace detail

/*!
@brief extract multiple JSON Pointer addressed subtrees from a JSON input in a single SAX pass

Parses @a input with full syntax validation, but only builds BasicJsonType
values for the subtrees addressed by @a pointers. Whenever a matched subtree
is complete, @a callback is invoked as callback(pointer, value); callbacks
arrive in the order in which subtrees complete in the document (a nested
match completes before its enclosing match). Returning false from the
callback stops the extraction early.

Requested pointers share a common-prefix trie, so parent and child pointers
can be requested together and are delivered independently; duplicate requests
are delivered only once. Pointers that never matched are reported together in
the result once the input is exhausted. Duplicate object members that would
make a requested path ambiguous are rejected by throwing
@ref detail::parse_error; duplicates on unrequested paths keep the default
parser behavior.

Memory usage is bounded by the requested-pointer trie, the currently open
(nested) matched subtrees, and per-object bookkeeping of relevant keys; it
never depends on the size of unselected parts of the input.

@since version 3.12.0
*/
template<typename BasicJsonType = basic_json<>, typename InputType, typename Callback>
sax_extract_result<typename BasicJsonType::string_t> sax_extract(InputType && input,
        const std::vector<typename BasicJsonType::string_t>& pointers,
        Callback && callback,
        const sax_extract_limits& limits = {})
{
    using callback_t = typename std::decay<Callback>::type;
    detail::json_multi_pointer_extractor<BasicJsonType, callback_t> extractor(pointers, callback, limits);
    return extractor.run(std::forward<InputType>(input));
}

NLOHMANN_JSON_NAMESPACE_END

