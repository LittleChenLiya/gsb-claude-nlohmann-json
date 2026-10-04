//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#ifndef INCLUDE_NLOHMANN_AUTHORIZED_EDIT_HPP_
#define INCLUDE_NLOHMANN_AUTHORIZED_EDIT_HPP_

#include <algorithm>  // equal
#include <cstddef>     // size_t
#include <limits>      // numeric_limits
#include <stdexcept>   // invalid_argument
#include <string>      // string
#include <utility>     // move
#include <vector>      // vector

#include <nlohmann/json.hpp>

namespace nlohmann
{
/// @brief explicit authorization layer for structural JSON edits
///
/// The caller hands a list of allowed JSON Pointer prefixes to an
/// @ref editor and then submits a batch of set/remove operations.
/// Authorization is decided on *decoded* reference tokens (RFC 6901):
/// the prefix "/a" covers "/a/0/name" but not "/a2", and "/a~1b" is the
/// single key "a/b", never the two levels "a" -> "b".  Array indices and
/// object string keys are resolved against the actual node type while
/// traversing, so the token "0" addresses an array element inside an
/// array and the key "0" inside an object.
///
/// All operations are first simulated, in order, on a private copy of
/// the document (structure and permission checks included).  Only when
/// every operation succeeds is the result published; on the first
/// failure the operation index and a reason are reported and the
/// original document is guaranteed to be left untouched.
namespace authorized_edit
{

/// @brief decode a JSON Pointer into unescaped reference tokens
/// @return true on success; on failure returns false and sets @a error
inline bool decode_pointer(const std::string& pointer,
                           std::vector<std::string>& tokens,
                           std::string& error)
{
    tokens.clear();
    if (pointer.empty())
    {
        return true; // empty pointer addresses the whole document
    }
    if (pointer.front() != '/')
    {
        error = "pointer must be empty or begin with '/'";
        return false;
    }
    std::string token;
    for (std::size_t i = 1; i < pointer.size(); ++i)
    {
        const char c = pointer[i];
        if (c == '/')
        {
            tokens.push_back(token);
            token.clear();
        }
        else if (c == '~')
        {
            // RFC 6901: '~0' -> '~', '~1' -> '/'; anything else is invalid
            if (i + 1 >= pointer.size() || (pointer[i + 1] != '0' && pointer[i + 1] != '1'))
            {
                error = "escape character '~' must be followed by '0' or '1'";
                return false;
            }
            token += (pointer[i + 1] == '0') ? '~' : '/';
            ++i;
        }
        else
        {
            token += c;
        }
    }
    tokens.push_back(token);
    return true;
}

/// @brief parse a reference token as an array index (RFC 6901 array syntax)
///
/// Accepts only decimal digits without leading zeros ("0" itself is fine).
inline bool parse_array_index(const std::string& token, std::size_t& index)
{
    if (token.empty() || (token.size() > 1 && token.front() == '0'))
    {
        return false;
    }
    std::size_t value = 0;
    for (const char c : token)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
        const auto digit = static_cast<std::size_t>(c - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
        {
            return false; // overflow
        }
        value = value * 10 + digit;
    }
    index = value;
    return true;
}

/// @brief check whether @a path is covered by at least one authorized prefix
///
/// Comparison is token-wise on already decoded tokens, so the prefix
/// ["a"] never matches the path ["a2"].
inline bool is_authorized(const std::vector<std::string>& path,
                          const std::vector<std::vector<std::string>>& prefixes)
{
    for (const auto& prefix : prefixes)
    {
        if (prefix.size() <= path.size() &&
                std::equal(prefix.begin(), prefix.end(), path.begin()))
        {
            return true;
        }
    }
    return false;
}

/// @brief a single set or remove operation addressed by a JSON Pointer
struct operation
{
    enum class op_type
    {
        set,   ///< create or replace the value at the pointer
        remove ///< remove the value (and its whole subtree) at the pointer
    };

    op_type type;       ///< what to do
    std::string pointer; ///< JSON Pointer to the target value
    json value;         ///< payload for @ref op_type::set, ignored otherwise

    static operation set(std::string ptr, json val)
    {
        operation op;
        op.type = op_type::set;
        op.pointer = std::move(ptr);
        op.value = std::move(val);
        return op;
    }

    static operation remove(std::string ptr)
    {
        operation op;
        op.type = op_type::remove;
        op.pointer = std::move(ptr);
        return op;
    }
};

/// @brief outcome of applying a batch of operations
struct result
{
    bool success = false;       ///< true iff every operation was applied
    std::size_t failed_index = 0; ///< index of the first failing operation (only when !success)
    std::string reason;         ///< human-readable failure reason (only when !success)
    json document;              ///< the published document (only when success)
};

namespace detail
{

inline result make_failure(std::size_t index, std::string reason)
{
    result res;
    res.success = false;
    res.failed_index = index;
    res.reason = std::move(reason);
    return res;
}

// navigate to the parent of the addressed value; returns nullptr and sets
// @a error when the parent path cannot be resolved
inline json* navigate_to_parent(json& doc,
                                const std::vector<std::string>& tokens,
                                std::string& error)
{
    json* node = &doc;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
    {
        const std::string& token = tokens[i];
        if (node->is_object())
        {
            auto it = node->find(token);
            if (it == node->end())
            {
                error = "parent path does not exist: object key '" + token + "' not found";
                return nullptr;
            }
            node = &(*it);
        }
        else if (node->is_array())
        {
            std::size_t index = 0;
            if (!parse_array_index(token, index))
            {
                error = "token '" + token + "' is not a valid array index";
                return nullptr;
            }
            if (index >= node->size())
            {
                error = "array index " + std::to_string(index) + " is out of range";
                return nullptr;
            }
            node = &(*node)[index];
        }
        else
        {
            error = "cannot descend into a non-container value at token '" + token + "'";
            return nullptr;
        }
    }
    return node;
}

// apply one already-authorized operation to the working document;
// returns an empty string on success, the failure reason otherwise
inline std::string apply_operation(json& doc,
                                   const std::vector<std::string>& tokens,
                                   const operation& op)
{
    std::string error;

    if (tokens.empty())
    {
        // the empty pointer addresses the whole document
        if (op.type == operation::op_type::set)
        {
            doc = op.value;
            return "";
        }
        return "removing the document root is not supported";
    }

    json* parent = navigate_to_parent(doc, tokens, error);
    if (parent == nullptr)
    {
        return error;
    }

    const std::string& last = tokens.back();

    if (op.type == operation::op_type::set)
    {
        if (parent->is_object())
        {
            (*parent)[last] = op.value; // create or replace the key
            return "";
        }
        if (parent->is_array())
        {
            if (last == "-") // RFC 6902 append marker
            {
                parent->push_back(op.value);
                return "";
            }
            std::size_t index = 0;
            if (!parse_array_index(last, index))
            {
                return "token '" + last + "' is not a valid array index";
            }
            if (index > parent->size())
            {
                return "array index " + std::to_string(index) + " is out of range";
            }
            if (index == parent->size())
            {
                parent->push_back(op.value);
            }
            else
            {
                (*parent)[index] = op.value;
            }
            return "";
        }
        return "cannot set a child of a non-container value";
    }

    // op_type::remove
    if (parent->is_object())
    {
        auto it = parent->find(last);
        if (it == parent->end())
        {
            return "object key '" + last + "' not found";
        }
        parent->erase(it);
        return "";
    }
    if (parent->is_array())
    {
        std::size_t index = 0;
        if (!parse_array_index(last, index))
        {
            return "token '" + last + "' is not a valid array index";
        }
        if (index >= parent->size())
        {
            return "array index " + std::to_string(index) + " is out of range";
        }
        // erasing shifts every subsequent element one position down;
        // later operations in the batch observe the shifted indices
        parent->erase(parent->begin() + static_cast<json::difference_type>(index));
        return "";
    }
    return "cannot remove a child of a non-container value";
}

} // namespace detail

/// @brief applies batches of set/remove operations under explicit path authorization
class editor
{
  public:
    /// @param allowed_prefixes JSON Pointers whose decoded token sequences
    ///        authorize the addressed value and everything below it;
    ///        the empty pointer "" authorizes the whole document
    /// @throws std::invalid_argument if a prefix is not a valid JSON Pointer
    explicit editor(const std::vector<std::string>& allowed_prefixes)
    {
        for (const auto& prefix : allowed_prefixes)
        {
            std::vector<std::string> tokens;
            std::string error;
            if (!decode_pointer(prefix, tokens, error))
            {
                throw std::invalid_argument("invalid authorized prefix '" + prefix + "': " + error);
            }
            prefixes_.push_back(std::move(tokens));
        }
    }

    /// @brief simulate all operations in order, then publish on full success
    ///
    /// The @a original document is never modified.  On the first failing
    /// operation the returned result reports the operation index and the
    /// reason, and no partial modification is published.
    result apply(const json& original, const std::vector<operation>& operations) const
    {
        json working = original; // simulate on a private copy
        for (std::size_t i = 0; i < operations.size(); ++i)
        {
            const operation& op = operations[i];

            std::vector<std::string> tokens;
            std::string error;
            if (!decode_pointer(op.pointer, tokens, error))
            {
                return detail::make_failure(i, "invalid JSON Pointer '" + op.pointer + "': " + error);
            }

            if (!is_authorized(tokens, prefixes_))
            {
                return detail::make_failure(i, "path '" + op.pointer + "' is not covered by any authorized prefix");
            }

            try
            {
                error = detail::apply_operation(working, tokens, op);
            }
            catch (const std::exception& e)
            {
                return detail::make_failure(i, std::string("operation failed: ") + e.what());
            }
            if (!error.empty())
            {
                return detail::make_failure(i, error);
            }
        }

        // every operation succeeded: publish the simulated document
        result res;
        res.success = true;
        res.document = std::move(working);
        return res;
    }

  private:
    std::vector<std::vector<std::string>> prefixes_; ///< decoded authorized prefixes
};

} // namespace authorized_edit
} // namespace nlohmann

#endif // INCLUDE_NLOHMANN_AUTHORIZED_EDIT_HPP_
