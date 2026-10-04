//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cctype> // isdigit
#include <cerrno> // errno, ERANGE
#include <cstdlib> // strtoull
#include <limits> // numeric_limits
#include <string> // string
#include <utility> // move, pair
#include <vector> // vector

#include <nlohmann/json.hpp>
#include <nlohmann/detail/json_pointer.hpp>
#include <nlohmann/detail/string_concat.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

/// @brief the type of an authorized edit operation
enum class authorized_operation_type
{
    set,    ///< add or replace a value at a path
    remove  ///< remove the value at a path
};

/// @brief a single operation in an authorized edit
struct authorized_operation
{
    authorized_operation_type type;
    json::json_pointer path;
    json value; ///< value for set operations; ignored for remove
};

/// @brief the result of applying an authorized edit
struct authorized_edit_result
{
    bool success = true;                ///< whether all operations were applied
    std::size_t failed_operation = 0;   ///< 0-based index of the failed operation
    std::string reason;                 ///< human-readable reason for the failure

    authorized_edit_result() = default;
    authorized_edit_result(bool s, std::size_t f, std::string r)
        : success(s), failed_operation(f), reason(std::move(r))
    {}
};

/// @brief an explicit authorization layer for JSON editing
///
/// The caller provides a set of allowed JSON Pointer prefixes, then submits
/// a set of set and remove operations. Path authorization is based on decoded
/// reference tokens: /a does not allow touching /a2, and array indices must not
/// be confused with object string keys. Removing a parent node deletes its
/// entire subtree, so the caller must have permission for that parent node.
///
/// All operations are simulated in order on a copy of the document. If any
/// operation fails (due to authorization or structural validity), the entire
/// edit is rejected: the result carries the operation index and reason, and
/// the original document is left unchanged.
class authorized_json_editor
{
  public:
    /// @brief add an authorized prefix
    /// @param prefix a JSON Pointer prefix; any path below it is authorized
    void authorize(const json::json_pointer& prefix)
    {
        authorized_prefixes_.push_back(prefix);
    }

    /// @brief add an authorized prefix from a string
    void authorize(const std::string& prefix)
    {
        authorize(json::json_pointer(prefix));
    }

    /// @brief add a set operation (add or replace)
    /// @param path the JSON Pointer path to set
    /// @param value the value to set
    void set(const json::json_pointer& path, json value)
    {
        operations_.push_back({authorized_operation_type::set, path, std::move(value)});
    }

    /// @brief add a set operation from a string path
    void set(const std::string& path, json value)
    {
        set(json::json_pointer(path), std::move(value));
    }

    /// @brief add a remove operation
    /// @param path the JSON Pointer path to remove
    void remove(const json::json_pointer& path)
    {
        operations_.push_back({authorized_operation_type::remove, path, json()});
    }

    /// @brief add a remove operation from a string path
    void remove(const std::string& path)
    {
        remove(json::json_pointer(path));
    }

    /// @brief return the number of submitted operations
    std::size_t operation_count() const noexcept
    {
        return operations_.size();
    }

    /// @brief remove all submitted operations (authorized prefixes are kept)
    void clear_operations() noexcept
    {
        operations_.clear();
    }

    /// @brief remove all authorized prefixes
    void clear_authorizations() noexcept
    {
        authorized_prefixes_.clear();
    }

    /// @brief simulate all operations in order and apply them if all are valid
    /// @param doc the JSON document to edit
    /// @return the result of the edit; on failure the document is unchanged
    authorized_edit_result apply(json& doc) const
    {
        // Simulate on a copy so a failure leaves the original untouched.
        json simulation = doc;

        for (std::size_t i = 0; i < operations_.size(); ++i)
        {
            const auto& op = operations_[i];

            // Check authorization first.
            if (!is_authorized(op.path))
            {
                return authorized_edit_result{false, i, detail::concat("operation not authorized: no allowed prefix matches '", op.path.to_string(), "'")};
            }

            // Check structural validity and apply to the simulation.
            std::string error;
            if (!apply_operation(simulation, op, error))
            {
                return authorized_edit_result{false, i, error};
            }
        }

        // All operations succeeded; publish the result.
        doc = std::move(simulation);
        return authorized_edit_result{true, 0, ""};
    }

  private:
    std::vector<json::json_pointer> authorized_prefixes_;
    std::vector<authorized_operation> operations_;

    /// @brief check whether a path is authorized by any of the prefixes
    bool is_authorized(const json::json_pointer& path) const
    {
        for (const auto& prefix : authorized_prefixes_)
        {
            if (prefix.is_prefix_of(path))
            {
                return true;
            }
        }
        return false;
    }

    /// @brief apply a single operation to the simulation document
    static bool apply_operation(json& doc, const authorized_operation& op, std::string& error)
    {
        switch (op.type)
        {
            case authorized_operation_type::set:
                return apply_set(doc, op.path, op.value, error);
            case authorized_operation_type::remove:
                return apply_remove(doc, op.path, error);
        }
        return false;
    }

    /// @brief parse a string as an array index following RFC 6901 rules
    static bool parse_array_index(const std::string& s, std::size_t& result)
    {
        if (s.empty())
        {
            return false;
        }

        // Leading zeros are invalid (except "0" itself).
        if (s.size() > 1 && s[0] == '0')
        {
            return false;
        }

        // All characters must be digits.
        for (char c : s)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
            {
                return false;
            }
        }

        errno = 0;
        const char* p = s.data();
        char* p_end = nullptr;
        const unsigned long long res = std::strtoull(p, &p_end, 10);

        if (p == p_end || errno == ERANGE)
        {
            return false;
        }

        if (static_cast<std::size_t>(p_end - p) != s.size())
        {
            return false;
        }

        if (res >= static_cast<unsigned long long>((std::numeric_limits<std::size_t>::max)()))
        {
            return false;
        }

        result = static_cast<std::size_t>(res);
        return true;
    }

    /// @brief apply a set operation (add or replace)
    static bool apply_set(json& doc, const json::json_pointer& path, const json& value, std::string& error)
    {
        // Root set replaces the entire document.
        if (path.empty())
        {
            doc = value;
            return true;
        }

        const json::json_pointer parent_path = path.parent_pointer();
        const std::string& last_token = path.back();

        // The parent must exist (unless the path is root).
        if (!parent_path.empty() && !doc.contains(parent_path))
        {
            error = detail::concat("set failed: parent path '", parent_path.to_string(), "' does not exist");
            return false;
        }

        json& parent = parent_path.empty() ? doc : doc.at(parent_path);

        if (parent.is_object())
        {
            parent[last_token] = value;
            return true;
        }

        if (parent.is_array())
        {
            // Special case: append to back.
            if (last_token == "-")
            {
                parent.push_back(value);
                return true;
            }

            std::size_t idx = 0;
            if (!parse_array_index(last_token, idx))
            {
                error = detail::concat("set failed: '", last_token, "' is not a valid array index");
                return false;
            }

            if (idx > parent.size())
            {
                error = detail::concat("set failed: array index ", std::to_string(idx),
                                       " is out of range (size ", std::to_string(parent.size()), ")");
                return false;
            }

            if (idx == parent.size())
            {
                parent.push_back(value);
            }
            else
            {
                // set replaces the existing value at the given index
                parent[idx] = value;
            }
            return true;
        }

        error = detail::concat("set failed: parent at '", parent_path.to_string(),
                                "' is of type ", parent.type_name(), ", but must be an object or array");
        return false;
    }

    /// @brief apply a remove operation
    static bool apply_remove(json& doc, const json::json_pointer& path, std::string& error)
    {
        // Root remove clears the document.
        if (path.empty())
        {
            doc = nullptr;
            return true;
        }

        const json::json_pointer parent_path = path.parent_pointer();
        const std::string& last_token = path.back();

        // The path must exist.
        if (!doc.contains(path))
        {
            error = detail::concat("remove failed: path '", path.to_string(), "' does not exist");
            return false;
        }

        json& parent = parent_path.empty() ? doc : doc.at(parent_path);

        if (parent.is_object())
        {
            auto it = parent.find(last_token);
            if (it == parent.end())
            {
                error = detail::concat("remove failed: key '", last_token, "' not found");
                return false;
            }
            parent.erase(it);
            return true;
        }

        if (parent.is_array())
        {
            std::size_t idx = 0;
            if (!parse_array_index(last_token, idx))
            {
                error = detail::concat("remove failed: '", last_token, "' is not a valid array index");
                return false;
            }

            if (idx >= parent.size())
            {
                error = detail::concat("remove failed: array index ", std::to_string(idx),
                                       " is out of range (size ", std::to_string(parent.size()), ")");
                return false;
            }

            parent.erase(parent.begin() + static_cast<json::difference_type>(idx));
            return true;
        }

        error = detail::concat("remove failed: parent at '", parent_path.to_string(),
                                "' is of type ", parent.type_name(), ", but must be an object or array");
        return false;
    }
};

NLOHMANN_JSON_NAMESPACE_END
