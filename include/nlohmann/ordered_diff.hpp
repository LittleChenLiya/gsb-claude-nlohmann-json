//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm> // is_permutation
#include <cstddef> // size_t
#include <string> // string
#include <utility> // move
#include <vector> // vector

#include <nlohmann/json.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

/// @brief explicit structural diff for nlohmann::ordered_json
///
/// Unlike nlohmann::json::diff (RFC 6902), this tool treats object member
/// order as significant, because ordered_json preserves insertion order.
/// It reports value changes and member-order changes as separate change
/// kinds. Plain nlohmann::json keeps its unordered object semantics and is
/// not affected by this header.
///
/// Comparison rules:
/// - objects are compared member by member, using the actual member order
///   of each side (recursively for nested objects);
/// - arrays keep positional semantics (element i vs. element i);
/// - numbers (and all other scalars) are compared with the library's own
///   operator==, i.e. along the library's native number types;
/// - order changes are reported by member name, never by iterator;
/// - options::ignore_object_order only hides order changes; value changes
///   are still reported;
/// - both input documents are left unchanged.
namespace ordered_diff
{

/// @brief the kind of a single reported change
enum class change_kind
{
    value_changed,  ///< same location, different scalar value or different JSON type
    order_changed,  ///< same object members, different member order
    added,          ///< member or array element only present in the new document
    removed         ///< member or array element only present in the old document
};

/// @brief options controlling the diff
struct options
{
    /// Hide order_changed entries. Value changes are still reported.
    bool ignore_object_order = false;
};

/// @brief a single reported difference
///
/// For change_kind::order_changed, old_members/new_members hold the member
/// names of the object at @a path in their respective order. For the other
/// kinds, old_value/new_value hold the differing values (a discarded value
/// on the side where nothing exists).
struct change
{
    change_kind kind = change_kind::value_changed;
    std::string path;                        ///< JSON pointer to the differing node
    std::vector<std::string> old_members;    ///< member names, old order (order_changed only)
    std::vector<std::string> new_members;    ///< member names, new order (order_changed only)
    ordered_json old_value;                  ///< old value (value_changed/removed)
    ordered_json new_value;                  ///< new value (value_changed/added)
};

/// @brief the outcome of a diff: an ordered list of changes
class result
{
  public:
    std::vector<change> changes;

    /// @brief whether any value change (value_changed/added/removed) was found
    bool has_value_changes() const
    {
        for (const auto& c : changes)
        {
            if (c.kind != change_kind::order_changed)
            {
                return true;
            }
        }
        return false;
    }

    /// @brief whether any member-order change was found
    bool has_order_changes() const
    {
        for (const auto& c : changes)
        {
            if (c.kind == change_kind::order_changed)
            {
                return true;
            }
        }
        return false;
    }

    /// @brief whether the two documents are identical (given the options)
    bool empty() const
    {
        return changes.empty();
    }

    /// @brief human-readable rendering, one line per change
    std::string str() const
    {
        std::string out;
        for (const auto& c : changes)
        {
            switch (c.kind)
            {
                case change_kind::value_changed:
                    out += "value changed at " + c.path + ": "
                           + c.old_value.dump() + " -> " + c.new_value.dump() + "\n";
                    break;
                case change_kind::order_changed:
                    out += "order changed at " + c.path + ": "
                           + render_members(c.old_members) + " -> "
                           + render_members(c.new_members) + "\n";
                    break;
                case change_kind::added:
                    out += "added at " + c.path + ": " + c.new_value.dump() + "\n";
                    break;
                case change_kind::removed:
                    out += "removed at " + c.path + ": " + c.old_value.dump() + "\n";
                    break;
            }
        }
        return out;
    }

  private:
    static std::string render_members(const std::vector<std::string>& names)
    {
        std::string out = "[";
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (i != 0)
            {
                out += ", ";
            }
            out += names[i];
        }
        out += "]";
        return out;
    }
};

namespace detail
{

inline std::string ordered_diff_escape(const std::string& s)
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

inline std::vector<std::string> ordered_diff_member_names(const ordered_json& j)
{
    std::vector<std::string> names;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        names.push_back(it.key());
    }
    return names;
}

inline void ordered_diff_impl(const ordered_json& old_doc, const ordered_json& new_doc,
                              const std::string& path, const options& opt,
                              std::vector<change>& out)
{
    // different JSON types: report as a single value change; numbers are the
    // exception, because the library compares them along its native number
    // types (e.g. 1 == 1.0), so they fall through to the scalar comparison
    if (old_doc.type() != new_doc.type()
            && !(old_doc.is_number() && new_doc.is_number()))
    {
        change c;
        c.kind = change_kind::value_changed;
        c.path = path;
        c.old_value = old_doc;
        c.new_value = new_doc;
        out.push_back(std::move(c));
        return;
    }

    if (old_doc.is_object())
    {
        const std::vector<std::string> old_names = ordered_diff_member_names(old_doc);
        const std::vector<std::string> new_names = ordered_diff_member_names(new_doc);

        // removed members (present in old, missing in new)
        for (const auto& name : old_names)
        {
            if (!new_doc.contains(name))
            {
                change c;
                c.kind = change_kind::removed;
                c.path = path + "/" + ordered_diff_escape(name);
                c.old_value = old_doc.at(name);
                out.push_back(std::move(c));
            }
        }

        // added members (present in new, missing in old)
        for (const auto& name : new_names)
        {
            if (!old_doc.contains(name))
            {
                change c;
                c.kind = change_kind::added;
                c.path = path + "/" + ordered_diff_escape(name);
                c.new_value = new_doc.at(name);
                out.push_back(std::move(c));
            }
        }

        // common members: recurse by member name (nested objects keep their
        // own actual order; arrays keep positional semantics)
        for (const auto& name : old_names)
        {
            if (new_doc.contains(name))
            {
                ordered_diff_impl(old_doc.at(name), new_doc.at(name),
                                  path + "/" + ordered_diff_escape(name), opt, out);
            }
        }

        // member order: only meaningful when both sides hold the same set of
        // members; reported by member name, never by iterator
        if (!opt.ignore_object_order
                && old_names.size() == new_names.size()
                && std::is_permutation(old_names.begin(), old_names.end(), new_names.begin())
                && old_names != new_names)
        {
            change c;
            c.kind = change_kind::order_changed;
            c.path = path;
            c.old_members = old_names;
            c.new_members = new_names;
            out.push_back(std::move(c));
        }
        return;
    }

    if (old_doc.is_array())
    {
        const std::size_t common = (std::min)(old_doc.size(), new_doc.size());
        for (std::size_t i = 0; i < common; ++i)
        {
            ordered_diff_impl(old_doc.at(i), new_doc.at(i),
                              path + "/" + std::to_string(i), opt, out);
        }
        for (std::size_t i = common; i < old_doc.size(); ++i)
        {
            change c;
            c.kind = change_kind::removed;
            c.path = path + "/" + std::to_string(i);
            c.old_value = old_doc.at(i);
            out.push_back(std::move(c));
        }
        for (std::size_t i = common; i < new_doc.size(); ++i)
        {
            change c;
            c.kind = change_kind::added;
            c.path = path + "/" + std::to_string(i);
            c.new_value = new_doc.at(i);
            out.push_back(std::move(c));
        }
        return;
    }

    // scalars (including numbers): compare with the library's own
    // operator==, i.e. along the library's native number types
    if (old_doc != new_doc)
    {
        change c;
        c.kind = change_kind::value_changed;
        c.path = path;
        c.old_value = old_doc;
        c.new_value = new_doc;
        out.push_back(std::move(c));
    }
}

} // namespace detail

/// @brief compute the explicit diff between two ordered_json documents
/// @param old_doc the original document (left unchanged)
/// @param new_doc the new document (left unchanged)
/// @param opt diff options (e.g. ignore object member order)
/// @return the list of changes; empty if the documents are identical
inline result diff(const ordered_json& old_doc, const ordered_json& new_doc,
                   const options& opt = options())
{
    result res;
    detail::ordered_diff_impl(old_doc, new_doc, "", opt, res.changes);
    return res;
}

} // namespace ordered_diff

NLOHMANN_JSON_NAMESPACE_END
