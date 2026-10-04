//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (add-on)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2025 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#ifndef NLOHMANN_JSON_PATCH_PREFLIGHT_HPP
#define NLOHMANN_JSON_PATCH_PREFLIGHT_HPP

#include <cerrno>   // errno, ERANGE
#include <cstddef>  // size_t
#include <cstdlib>  // strtoull
#include <limits>   // numeric_limits
#include <string>   // string, to_string
#include <utility>  // move
#include <vector>   // vector

#include <nlohmann/json.hpp>

namespace nlohmann
{

/// @brief preflight checking of JSON Patches (RFC 6902) on an internal copy
///
/// This namespace provides check(), a dry-run evaluator for JSON Patches.
/// The patch is executed operation by operation on an internal copy of the
/// target document - never on the caller's document, never on the caller's
/// patch - and every operation is reported as applied, failed, or blocked.
/// Execution stops at the first failed operation; all remaining operations
/// are marked blocked and are not executed on the by-then uncertain
/// document state. Because operations are genuinely executed (not merely
/// syntax-checked), "test" outcomes and the existence of the addressed
/// values are verified for real.
///
/// Requires exceptions (the implementation relies on try/catch around the
/// same primitives basic_json::patch_inplace() uses).
namespace patch_preflight
{

/// @brief outcome of a single patch operation during preflight
enum class op_status
{
    applied, ///< the operation was executed on the internal copy and succeeded
    failed,  ///< the operation was executed on the internal copy and failed
    blocked  ///< the operation was not executed because an earlier operation failed
};

/// @brief the pointer of an operation a failure is attributed to
enum class pointer_role
{
    none, ///< no pointer involved (structural problem, e.g. an invalid "op" member)
    path, ///< the operation's "path" pointer
    from  ///< the operation's "from" pointer (move/copy only)
};

/// @brief per-operation report entry; keeps the operation index and pointers
struct op_report
{
    std::size_t index = 0;       ///< position of the operation within the patch
    std::string op;              ///< value of the "op" member ("" if not a string)
    std::string path;            ///< raw JSON Pointer of the "path" member ("" addresses the document root)
    bool has_from = false;       ///< whether a string "from" member was present
    std::string from;            ///< raw JSON Pointer of the "from" member (move/copy)

    op_status status = op_status::applied;       ///< outcome of this operation
    pointer_role failed_at = pointer_role::none; ///< pointer the failure is attributed to (failed operations only)
    std::string message;         ///< diagnostic detail (failed operations only)
};

/// @brief result of a preflight check
struct report
{
    bool ok = true;        ///< true iff every operation of the patch applied
    std::string error;     ///< patch-level problem (e.g. the patch is not an array); operations is empty then
    std::size_t failed_index = 0;        ///< index of the first failed operation (valid iff !ok && error.empty())
    std::vector<op_report> operations;   ///< one entry per patch operation, in patch order
    nlohmann::json document;             ///< internal copy after the applied prefix; the input document is never modified
};

/// @brief returns the name of an op_status value
inline const char* to_string(op_status status)
{
    switch (status)
    {
        case op_status::applied:
        {
            return "applied";
        }
        case op_status::failed:
        {
            return "failed";
        }
        case op_status::blocked:
        {
            return "blocked";
        }
        default:
        {
            return "unknown";
        }
    }
}

/// @brief returns the name of a pointer_role value
inline const char* to_string(pointer_role role)
{
    switch (role)
    {
        case pointer_role::path:
        {
            return "path";
        }
        case pointer_role::from:
        {
            return "from";
        }
        case pointer_role::none:
        default:
        {
            return "none";
        }
    }
}

namespace detail
{

using json = nlohmann::json;
using json_pointer = json::json_pointer;

enum class patch_operation
{
    add, remove, replace, move, copy, test, invalid
};

inline patch_operation get_op(const std::string& op)
{
    if (op == "add")
    {
        return patch_operation::add;
    }
    if (op == "remove")
    {
        return patch_operation::remove;
    }
    if (op == "replace")
    {
        return patch_operation::replace;
    }
    if (op == "move")
    {
        return patch_operation::move;
    }
    if (op == "copy")
    {
        return patch_operation::copy;
    }
    if (op == "test")
    {
        return patch_operation::test;
    }
    return patch_operation::invalid;
}

/// @brief returns a pointer to member @a key of @a object, or nullptr
inline const json* find_member(const json& object, const char* key)
{
    const auto it = object.find(key);
    return (it != object.end()) ? &(*it) : nullptr;
}

/// @brief converts a reference token to an array index
///
/// Mirrors nlohmann::detail::json_pointer::array_index (which is private):
/// same acceptance rules and same exceptions (parse_error.106,
/// parse_error.109, out_of_range.404, out_of_range.410).
inline std::size_t array_index(const std::string& s)
{
    // error condition (cf. RFC 6901, Sect. 4)
    if (s.size() > 1 && s[0] == '0')
    {
        throw nlohmann::detail::parse_error::create(106, 0, "array index '" + s + "' must not begin with '0'", nullptr);
    }

    // error condition (cf. RFC 6901, Sect. 4)
    if (s.size() > 1 && !(s[0] >= '1' && s[0] <= '9'))
    {
        throw nlohmann::detail::parse_error::create(109, 0, "array index '" + s + "' is not a number", nullptr);
    }

    const char* p = s.c_str();
    char* p_end = nullptr;
    errno = 0; // strtoull doesn't reset errno
    const unsigned long long res = std::strtoull(p, &p_end, 10); // NOLINT(runtime/int)
    if (p == p_end // invalid input or empty string
            || errno == ERANGE // out of range
            || static_cast<std::size_t>(p_end - p) != s.size()) // incomplete read
    {
        throw nlohmann::detail::out_of_range::create(404, "unresolved reference token '" + s + "'", nullptr);
    }

    if (res >= static_cast<unsigned long long>((std::numeric_limits<json::size_type>::max)())) // NOLINT(runtime/int)
    {
        throw nlohmann::detail::out_of_range::create(410, "array index " + s + " exceeds size_type", nullptr);
    }

    return static_cast<std::size_t>(res);
}

/// @brief "add" operation; mirrors basic_json::patch_inplace()
inline void operation_add(json& result, const json_pointer& ptr, const json& val)
{
    // adding to the root of the target document means replacing it
    if (ptr.empty())
    {
        result = val;
        return;
    }

    // make sure the top element of the pointer exists
    json_pointer top_pointer;
    top_pointer /= ptr.front();
    if (top_pointer != ptr)
    {
        result.at(top_pointer);
    }

    // get reference to the parent of the JSON pointer ptr
    const std::string last_path = ptr.back();
    // parent must exist when performing patch add per RFC6902 specs
    json& parent = result.at(ptr.parent_pointer());

    if (parent.is_null() || parent.is_object())
    {
        // use operator[] to add value
        parent[last_path] = val;
    }
    else if (parent.is_array())
    {
        if (last_path == "-")
        {
            // special case: append to back
            parent.push_back(val);
        }
        else
        {
            const std::size_t idx = array_index(last_path);
            if (idx > parent.size())
            {
                // avoid undefined behavior
                throw nlohmann::detail::out_of_range::create(401, "array index " + std::to_string(idx) + " is out of range", &parent);
            }

            // default case: insert at offset
            parent.insert(parent.begin() + static_cast<json::difference_type>(idx), val);
        }
    }
    else
    {
        // the parent of an "add" target must be an object or array
        throw nlohmann::detail::out_of_range::create(411, std::string("cannot add value: the JSON Patch 'add' target's parent is of type ") + parent.type_name() + ", but must be an object or array", &parent);
    }
}

/// @brief "remove" operation; mirrors basic_json::patch_inplace()
inline void operation_remove(json& result, const json_pointer& ptr)
{
    // get reference to the parent of the JSON pointer ptr
    // (back() throws out_of_range.405 for the root pointer, exactly like
    // the json_pointer used by patch_inplace())
    const std::string last_path = ptr.back();
    json& parent = result.at(ptr.parent_pointer());

    // remove child
    if (parent.is_object())
    {
        // perform range check
        const auto it = parent.find(last_path);
        if (it != parent.end())
        {
            parent.erase(it);
        }
        else
        {
            throw nlohmann::detail::out_of_range::create(403, "key '" + last_path + "' not found", &result);
        }
    }
    else if (parent.is_array())
    {
        // note: erase performs the range check; a "-" token is rejected by
        // array_index() - "-" is only valid as the last token of an "add"
        parent.erase(array_index(last_path));
    }
    else
    {
        // the parent of a "remove" target must be an object or array
        throw nlohmann::detail::out_of_range::create(413, std::string("cannot remove value: the JSON Patch 'remove' target's parent is of type ") + parent.type_name() + ", but must be an object or array", &parent);
    }
}

/// @brief RFC 6902 (section 4.4): "from" must not be a proper prefix of "path"
///
/// "from" is a proper prefix of "to" iff it equals one of to's strict
/// ancestors. Compares parsed pointers (reference tokens, already
/// unescaped) rather than raw pointer strings, so escaped tokens cannot
/// defeat the check. "from" equal to "path" is not a proper prefix.
inline bool is_proper_prefix(const json_pointer& from, const json_pointer& to)
{
    json_pointer ancestor = to;
    while (!ancestor.empty())
    {
        ancestor = ancestor.parent_pointer();
        if (ancestor == from)
        {
            return true;
        }
    }
    return false;
}

/// @brief records the operation's identifying members without executing it
///
/// Used for blocked operations: the raw "op"/"path"/"from" members are
/// copied into the report for orientation, but nothing is validated or
/// executed, so a blocked entry never claims that a "test" would have
/// passed or that a target exists.
inline void fill_identifiers(const json& val, op_report& out)
{
    if (!val.is_object())
    {
        return;
    }
    const json* member = find_member(val, "op");
    if (member != nullptr && member->is_string())
    {
        out.op = member->get_ref<const std::string&>();
    }
    member = find_member(val, "path");
    if (member != nullptr && member->is_string())
    {
        out.path = member->get_ref<const std::string&>();
    }
    member = find_member(val, "from");
    if (member != nullptr && member->is_string())
    {
        out.has_from = true;
        out.from = member->get_ref<const std::string&>();
    }
}

/// @brief executes a single patch operation on the internal copy @a doc
///
/// On return, out.status is op_status::applied iff the operation succeeded;
/// otherwise out.status is op_status::failed and out.failed_at/out.message
/// describe which pointer ("from" vs. "path") caused the failure and why.
inline void execute_one(json& doc, const json& val, op_report& out)
{
    out.status = op_status::failed; // proven otherwise below

    // type check: every element of the patch array must be an object
    if (!val.is_object())
    {
        out.message = "JSON patch must be an array of objects";
        return;
    }

    // collect the mandatory "op" member
    const json* member = find_member(val, "op");
    if (member == nullptr)
    {
        out.message = "operation must have member 'op'";
        return;
    }
    if (!member->is_string())
    {
        out.message = "operation must have string member 'op'";
        return;
    }
    out.op = member->get_ref<const std::string&>();

    // collect the mandatory "path" member
    member = find_member(val, "path");
    if (member == nullptr)
    {
        out.message = "operation '" + out.op + "' must have member 'path'";
        return;
    }
    if (!member->is_string())
    {
        out.message = "operation '" + out.op + "' must have string member 'path'";
        return;
    }
    out.path = member->get_ref<const std::string&>();

    // keep a string "from" member for the report (validated for move/copy below)
    member = find_member(val, "from");
    if (member != nullptr && member->is_string())
    {
        out.has_from = true;
        out.from = member->get_ref<const std::string&>();
    }

    try
    {
        // from here on a failure is attributed to the "path" pointer unless
        // a branch re-attributes it to the "from" pointer
        out.failed_at = pointer_role::path;
        json_pointer ptr(out.path);

        switch (get_op(out.op))
        {
            case patch_operation::add:
            {
                member = find_member(val, "value");
                if (member == nullptr)
                {
                    out.failed_at = pointer_role::none;
                    out.message = "operation 'add' must have member 'value'";
                    return;
                }
                operation_add(doc, ptr, *member);
                break;
            }

            case patch_operation::remove:
            {
                operation_remove(doc, ptr);
                break;
            }

            case patch_operation::replace:
            {
                member = find_member(val, "value");
                if (member == nullptr)
                {
                    out.failed_at = pointer_role::none;
                    out.message = "operation 'replace' must have member 'value'";
                    return;
                }
                // the "path" location must exist - use at()
                doc.at(ptr) = *member;
                break;
            }

            case patch_operation::move:
            {
                member = find_member(val, "from");
                if (member == nullptr)
                {
                    out.failed_at = pointer_role::from;
                    out.message = "operation 'move' must have member 'from'";
                    return;
                }
                if (!member->is_string())
                {
                    out.failed_at = pointer_role::from;
                    out.message = "operation 'move' must have string member 'from'";
                    return;
                }
                out.has_from = true;
                out.from = member->get_ref<const std::string&>();

                out.failed_at = pointer_role::from;
                const json_pointer from_ptr(out.from);

                if (is_proper_prefix(from_ptr, ptr))
                {
                    out.message = "cannot move value: 'from' path '" + out.from + "' is a proper prefix of 'path' '" + out.path + "'";
                    return;
                }

                // the "from" location must exist - use at()
                const json v = doc.at(from_ptr);

                // functionally identical to a "remove" on "from" followed by
                // an "add" at "path" with the removed value
                operation_remove(doc, from_ptr);
                out.failed_at = pointer_role::path;
                operation_add(doc, ptr, v);
                break;
            }

            case patch_operation::copy:
            {
                member = find_member(val, "from");
                if (member == nullptr)
                {
                    out.failed_at = pointer_role::from;
                    out.message = "operation 'copy' must have member 'from'";
                    return;
                }
                if (!member->is_string())
                {
                    out.failed_at = pointer_role::from;
                    out.message = "operation 'copy' must have string member 'from'";
                    return;
                }
                out.has_from = true;
                out.from = member->get_ref<const std::string&>();

                out.failed_at = pointer_role::from;
                const json_pointer from_ptr(out.from);

                // the "from" location must exist - use at()
                const json v = doc.at(from_ptr);

                // functionally identical to an "add" at "path"
                out.failed_at = pointer_role::path;
                operation_add(doc, ptr, v);
                break;
            }

            case patch_operation::test:
            {
                member = find_member(val, "value");
                if (member == nullptr)
                {
                    out.failed_at = pointer_role::none;
                    out.message = "operation 'test' must have member 'value'";
                    return;
                }

                out.failed_at = pointer_role::path;
                bool exists = true;
                bool success = false;
                try
                {
                    // check if "value" matches the one at "path"
                    // the "path" location must exist - use at()
                    success = (doc.at(ptr) == *member);
                }
                catch (const json::out_of_range&)
                {
                    // ignore out of range errors: success remains false
                    exists = false;
                }

                if (!success)
                {
                    out.message = exists
                                  ? "test operation failed: value at path '" + out.path + "' does not match the expected value"
                                  : "test operation failed: path '" + out.path + "' does not exist";
                    return;
                }
                break;
            }

            case patch_operation::invalid:
            default:
            {
                // op must be "add", "remove", "replace", "move", "copy", or "test"
                out.failed_at = pointer_role::none;
                out.message = "operation value '" + out.op + "' is invalid";
                return;
            }
        }

        out.status = op_status::applied;
        out.failed_at = pointer_role::none;
    }
    catch (const json::exception& e)
    {
        out.message = e.what();
    }
}

} // namespace detail

/// @brief dry-runs a JSON Patch on an internal copy of @a document
///
/// Executes @a patch operation by operation on a copy of @a document,
/// following the semantics of basic_json::patch_inplace() (RFC 6902):
///
/// - each operation is reported as applied, failed, or blocked;
/// - the first failed operation stops execution - every later operation is
///   marked blocked and is not executed on the uncertain document state;
/// - failures are attributed to the "path" or "from" pointer that caused
///   them, and each entry keeps the operation index and the raw pointers;
/// - the array index "-" is only accepted where the patch semantics allow
///   it (as the last token of an "add" path, i.e. append) and is rejected
///   everywhere else;
/// - neither @a document nor @a patch is modified; report::document holds
///   the internal copy after the applied prefix of operations.
///
/// @param[in] document  the JSON value the patch would be applied to
/// @param[in] patch     the JSON Patch (an array of operation objects)
/// @return preflight report with one entry per patch operation
inline report check(const nlohmann::json& document, const nlohmann::json& patch)
{
    report rep;
    rep.document = document; // work on an internal copy only

    // type check: top level value must be an array
    if (!patch.is_array())
    {
        rep.ok = false;
        rep.error = "JSON patch must be an array of objects";
        return rep;
    }

    bool blocked = false;
    std::size_t index = 0;
    for (const auto& val : patch)
    {
        op_report entry;
        entry.index = index;

        if (blocked)
        {
            // an earlier operation failed: do not execute on the uncertain
            // state; only record the operation's identifiers
            detail::fill_identifiers(val, entry);
            entry.status = op_status::blocked;
        }
        else
        {
            detail::execute_one(rep.document, val, entry);
            if (entry.status == op_status::failed)
            {
                rep.ok = false;
                rep.failed_index = index;
                blocked = true;
            }
        }

        rep.operations.push_back(std::move(entry));
        ++index;
    }

    return rep;
}

} // namespace patch_preflight
} // namespace nlohmann

#endif // NLOHMANN_JSON_PATCH_PREFLIGHT_HPP
