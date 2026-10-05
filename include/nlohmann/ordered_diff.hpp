//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace nlohmann
{

/// Result of comparing two @ref ordered_json values.
///
/// @note @ref value_changes holds paths at which the values differ
///       (including type mismatches, added/removed members, and array
///       size mismatches). @ref order_changes holds paths at which the
///       same set of object members appears in a different order.
struct ordered_diff_result
{
    std::vector<std::string> value_changes;
    std::vector<std::string> order_changes;

    /// @return true if any value changes were reported.
    bool has_value_changes() const noexcept
    {
        return !value_changes.empty();
    }

    /// @return true if any object member order changes were reported.
    bool has_order_changes() const noexcept
    {
        return !order_changes.empty();
    }
};

namespace detail
{

/// Join a list of member names into a human-readable order description.
inline std::string ordered_diff_join_keys(const std::vector<std::string>& keys)
{
    std::string result = "[";
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (i > 0)
        {
            result += ", ";
        }
        result += keys[i];
    }
    result += "]";
    return result;
}

/// Recursively compare two @ref ordered_json values, reporting value
/// changes and object member order changes separately.
///
/// @param a            the original value
/// @param b            the value to compare against
/// @param path         the current JSON-pointer-like path
/// @param result       collects the reported differences
/// @param ignore_order if true, hide object member order differences
inline void ordered_diff_recursive(const ordered_json& a, const ordered_json& b,
                                   const std::string& path,
                                   ordered_diff_result& result,
                                   bool ignore_order)
{
    // Different types: report a value change (type mismatch).
    // This also distinguishes number_integer from number_float etc.,
    // because numbers are compared along the library's original type.
    if (a.type() != b.type())
    {
        result.value_changes.push_back(
            path + ": type mismatch (" + a.type_name() + " vs " + b.type_name() + ")");
        return;
    }

    switch (a.type())
    {
        case ordered_json::value_t::object:
        {
            // Collect member names in their actual order.
            std::vector<std::string> a_keys;
            std::vector<std::string> b_keys;
            for (const auto& item : a.items())
            {
                a_keys.push_back(item.key());
            }
            for (const auto& item : b.items())
            {
                b_keys.push_back(item.key());
            }

            // Build sets for membership tests.
            std::set<std::string> a_set(a_keys.begin(), a_keys.end());
            std::set<std::string> b_set(b_keys.begin(), b_keys.end());

            if (a_set != b_set)
            {
                // Members were added or removed: report as a value change.
                std::string added;
                std::string removed;
                for (const auto& k : b_keys)
                {
                    if (a_set.find(k) == a_set.end())
                    {
                        if (!added.empty())
                        {
                            added += ", ";
                        }
                        added += k;
                    }
                }
                for (const auto& k : a_keys)
                {
                    if (b_set.find(k) == b_set.end())
                    {
                        if (!removed.empty())
                        {
                            removed += ", ";
                        }
                        removed += k;
                    }
                }
                std::string msg = path + ": object members changed";
                if (!added.empty())
                {
                    msg += " (added: " + added + ")";
                }
                if (!removed.empty())
                {
                    msg += " (removed: " + removed + ")";
                }
                result.value_changes.push_back(msg);
            }

            // Same member set but different order: report as an order change.
            if (a_set == b_set && a_keys != b_keys && !ignore_order)
            {
                result.order_changes.push_back(
                    path + ": member order changed from " + ordered_diff_join_keys(a_keys) +
                    " to " + ordered_diff_join_keys(b_keys));
            }

            // Recurse into common members, in a's order.
            for (const auto& k : a_keys)
            {
                if (b_set.find(k) != b_set.end())
                {
                    ordered_diff_recursive(a.at(k), b.at(k), path + "." + k, result, ignore_order);
                }
            }
            break;
        }

        case ordered_json::value_t::array:
        {
            if (a.size() != b.size())
            {
                result.value_changes.push_back(
                    path + ": array size mismatch (" + std::to_string(a.size()) + " vs " +
                    std::to_string(b.size()) + ")");
            }
            // Arrays are positional: compare element by element.
            const std::size_t n = std::min(a.size(), b.size());
            for (std::size_t i = 0; i < n; ++i)
            {
                ordered_diff_recursive(a[i], b[i], path + "[" + std::to_string(i) + "]", result, ignore_order);
            }
            break;
        }

        case ordered_json::value_t::number_integer:
        case ordered_json::value_t::number_unsigned:
        case ordered_json::value_t::number_float:
        {
            // Same number type (guaranteed by the type check above).
            // Compare the stored values directly.
            if (a != b)
            {
                result.value_changes.push_back(path + ": number value changed");
            }
            break;
        }

        default:
        {
            // string, boolean, null, binary
            if (a != b)
            {
                result.value_changes.push_back(path + ": value changed");
            }
            break;
        }
    }
}

} // namespace detail

/// Compute an explicit diff between two @ref ordered_json values.
///
/// Reports value changes and object member order changes separately.
/// Nested objects are compared by their actual member order; arrays are
/// compared positionally. Numbers are compared along the library's
/// original type (so an integer and a float are reported as a type
/// mismatch). When @p ignore_order is true, order differences are not
/// reported, but value changes still are.
///
/// @param a            the original value
/// @param b            the value to compare against
/// @param ignore_order if true, hide object member order differences
/// @return an @ref ordered_diff_result with the reported differences
inline ordered_diff_result ordered_diff(const ordered_json& a, const ordered_json& b,
                                        bool ignore_order = false)
{
    ordered_diff_result result;
    detail::ordered_diff_recursive(a, b, "$", result, ignore_order);
    return result;
}

} // namespace nlohmann