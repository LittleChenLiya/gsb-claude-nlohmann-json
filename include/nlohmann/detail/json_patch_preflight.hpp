//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef> // size_t
#include <exception> // exception
#include <string> // string
#include <vector> // vector

#include <nlohmann/detail/exceptions.hpp>
#include <nlohmann/detail/json_pointer.hpp>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/string_concat.hpp>
#include <nlohmann/detail/value_t.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

/// @brief status of a single JSON Patch operation during preflight
/// @sa https://json.nlohmann.me/api/basic_json/preflight_patch/
enum class json_patch_preflight_status
{
    ok,      ///< the operation can be applied to the document
    failed,  ///< the operation cannot be applied; see @a message for the reason
    blocked  ///< the operation was not checked because an earlier operation failed
};

/// @brief report for a single JSON Patch operation
/// @sa https://json.nlohmann.me/api/basic_json/preflight_patch/
struct json_patch_preflight_operation
{
    /// @brief index of the operation in the patch array
    std::size_t index = 0;

    /// @brief operation name ("add", "remove", "replace", "move", "copy", "test")
    std::string op;

    /// @brief JSON Pointer from the operation's "path" member
    std::string path;

    /// @brief JSON Pointer from the operation's "from" member (empty if absent)
    std::string from;

    /// @brief status of the operation
    json_patch_preflight_status status = json_patch_preflight_status::ok;

    /// @brief human-readable diagnostic message (empty if status is ok)
    std::string message;
};

/// @brief report of preflighting a JSON Patch
/// @sa https://json.nlohmann.me/api/basic_json/preflight_patch/
struct json_patch_preflight_report
{
    /// @brief reports for each operation in the patch, in order
    std::vector<json_patch_preflight_operation> operations;

    /// @return true if every operation would succeed
    bool ok() const noexcept
    {
        for (const auto& op : operations)
        {
            if (op.status != json_patch_preflight_status::ok)
            {
                return false;
            }
        }
        return true;
    }
};

template<typename BasicJsonType>
class json_patch_preflight
{
  public:
    using report_type = json_patch_preflight_report;
    using operation_report_type = json_patch_preflight_operation;
    using string_t = typename BasicJsonType::string_t;
    using json_pointer_t = json_pointer<string_t>;

    static report_type check(const BasicJsonType& doc, const BasicJsonType& json_patch)
    {
        report_type report;
        BasicJsonType working = doc;
        bool blocked = false;

        enum class patch_operations { add, remove, replace, move, copy, test, invalid };

        const auto get_op = [](const string_t& op) -> patch_operations
        {
            if (op == "add") { return patch_operations::add; }
            if (op == "remove") { return patch_operations::remove; }
            if (op == "replace") { return patch_operations::replace; }
            if (op == "move") { return patch_operations::move; }
            if (op == "copy") { return patch_operations::copy; }
            if (op == "test") { return patch_operations::test; }
            return patch_operations::invalid;
        };

        if (JSON_HEDLEY_UNLIKELY(!json_patch.is_array()))
        {
            operation_report_type op_report;
            op_report.index = 0;
            op_report.status = json_patch_preflight_status::failed;
            op_report.message = "JSON patch must be an array of objects";
            report.operations.push_back(op_report);
            return report;
        }

        for (std::size_t i = 0; i < json_patch.size(); ++i)
        {
            const auto& val = json_patch[i];
            operation_report_type op_report;
            op_report.index = i;

            if (blocked)
            {
                op_report.status = json_patch_preflight_status::blocked;
                if (val.is_object())
                {
                    auto op_it = val.find("op");
                    if (op_it != val.end() && op_it->is_string())
                    {
                        op_report.op = op_it->template get<string_t>();
                    }
                    auto path_it = val.find("path");
                    if (path_it != val.end() && path_it->is_string())
                    {
                        op_report.path = path_it->template get<string_t>();
                    }
                    auto from_it = val.find("from");
                    if (from_it != val.end() && from_it->is_string())
                    {
                        op_report.from = from_it->template get<string_t>();
                    }
                }
                report.operations.push_back(op_report);
                continue;
            }

            if (JSON_HEDLEY_UNLIKELY(!val.is_object()))
            {
                op_report.status = json_patch_preflight_status::failed;
                op_report.message = "JSON patch must be an array of objects";
                report.operations.push_back(op_report);
                blocked = true;
                continue;
            }

            string_t op;
            try
            {
                auto op_it = val.find("op");
                if (op_it == val.end())
                {
                    op_report.status = json_patch_preflight_status::failed;
                    op_report.message = "operation must have member 'op'";
                    report.operations.push_back(op_report);
                    blocked = true;
                    continue;
                }
                if (JSON_HEDLEY_UNLIKELY(!op_it->is_string()))
                {
                    op_report.status = json_patch_preflight_status::failed;
                    op_report.message = "operation must have string member 'op'";
                    report.operations.push_back(op_report);
                    blocked = true;
                    continue;
                }
                op = op_it->template get<string_t>();
                op_report.op = op;
            }
            catch (...)
            {
                op_report.status = json_patch_preflight_status::failed;
                op_report.message = "operation must have member 'op'";
                report.operations.push_back(op_report);
                blocked = true;
                continue;
            }

            string_t path;
            try
            {
                auto path_it = val.find("path");
                if (path_it == val.end())
                {
                    op_report.status = json_patch_preflight_status::failed;
                    op_report.message = detail::concat("operation '", op, "' must have member 'path'");
                    report.operations.push_back(op_report);
                    blocked = true;
                    continue;
                }
                if (JSON_HEDLEY_UNLIKELY(!path_it->is_string()))
                {
                    op_report.status = json_patch_preflight_status::failed;
                    op_report.message = detail::concat("operation '", op, "' must have string member 'path'");
                    report.operations.push_back(op_report);
                    blocked = true;
                    continue;
                }
                path = path_it->template get<string_t>();
                op_report.path = path;
            }
            catch (...)
            {
                op_report.status = json_patch_preflight_status::failed;
                op_report.message = detail::concat("operation '", op, "' must have member 'path'");
                report.operations.push_back(op_report);
                blocked = true;
                continue;
            }

            json_pointer_t ptr;
            try
            {
                ptr = json_pointer_t(path);
            }
            catch (const std::exception& e)
            {
                op_report.status = json_patch_preflight_status::failed;
                op_report.message = detail::concat("operation '", op, "' has invalid 'path' pointer: ", e.what());
                report.operations.push_back(op_report);
                blocked = true;
                continue;
            }
            catch (...)
            {
                op_report.status = json_patch_preflight_status::failed;
                op_report.message = detail::concat("operation '", op, "' has invalid 'path' pointer");
                report.operations.push_back(op_report);
                blocked = true;
                continue;
            }

            bool op_ok = false;
            string_t op_message;

            try
            {
                switch (get_op(op))
                {
                    case patch_operations::add:
                    {
                        auto value_it = val.find("value");
                        if (value_it == val.end())
                        {
                            op_message = detail::concat("operation '", op, "' must have member 'value'");
                            break;
                        }
                        operation_add(working, ptr, *value_it);
                        op_ok = true;
                        break;
                    }

                    case patch_operations::remove:
                    {
                        operation_remove(working, ptr);
                        op_ok = true;
                        break;
                    }

                    case patch_operations::replace:
                    {
                        auto value_it = val.find("value");
                        if (value_it == val.end())
                        {
                            op_message = detail::concat("operation '", op, "' must have member 'value'");
                            break;
                        }
                        working.at(ptr) = *value_it;
                        op_ok = true;
                        break;
                    }

                    case patch_operations::move:
                    {
                        auto from_it = val.find("from");
                        if (from_it == val.end())
                        {
                            op_message = detail::concat("operation '", op, "' must have member 'from'");
                            break;
                        }
                        if (JSON_HEDLEY_UNLIKELY(!from_it->is_string()))
                        {
                            op_message = detail::concat("operation '", op, "' must have string member 'from'");
                            break;
                        }
                        string_t from_path = from_it->template get<string_t>();
                        op_report.from = from_path;

                        json_pointer_t from_ptr;
                        try
                        {
                            from_ptr = json_pointer_t(from_path);
                        }
                        catch (const std::exception& e)
                        {
                            op_message = detail::concat("operation '", op, "' has invalid 'from' pointer: ", e.what());
                            break;
                        }

                        BasicJsonType v;
                        try
                        {
                            v = working.at(from_ptr);
                        }
                        catch (const std::exception& e)
                        {
                            op_message = detail::concat("operation '", op, "' from path '", from_path, "': ", e.what());
                            break;
                        }

                        if (JSON_HEDLEY_UNLIKELY(is_proper_prefix(from_ptr, ptr)))
                        {
                            op_message = detail::concat("cannot move value: 'from' path '", from_path, "' is a proper prefix of 'path' '", path, "'");
                            break;
                        }

                        try
                        {
                            operation_remove(working, from_ptr);
                            operation_add(working, ptr, v);
                        }
                        catch (const std::exception& e)
                        {
                            op_message = detail::concat("operation '", op, "' path '", path, "': ", e.what());
                            break;
                        }
                        op_ok = true;
                        break;
                    }

                    case patch_operations::copy:
                    {
                        auto from_it = val.find("from");
                        if (from_it == val.end())
                        {
                            op_message = detail::concat("operation '", op, "' must have member 'from'");
                            break;
                        }
                        if (JSON_HEDLEY_UNLIKELY(!from_it->is_string()))
                        {
                            op_message = detail::concat("operation '", op, "' must have string member 'from'");
                            break;
                        }
                        string_t from_path = from_it->template get<string_t>();
                        op_report.from = from_path;

                        json_pointer_t from_ptr;
                        try
                        {
                            from_ptr = json_pointer_t(from_path);
                        }
                        catch (const std::exception& e)
                        {
                            op_message = detail::concat("operation '", op, "' has invalid 'from' pointer: ", e.what());
                            break;
                        }

                        BasicJsonType v;
                        try
                        {
                            v = working.at(from_ptr);
                        }
                        catch (const std::exception& e)
                        {
                            op_message = detail::concat("operation '", op, "' from path '", from_path, "': ", e.what());
                            break;
                        }
                        operation_add(working, ptr, v);
                        op_ok = true;
                        break;
                    }

                    case patch_operations::test:
                    {
                        auto value_it = val.find("value");
                        if (value_it == val.end())
                        {
                            op_message = detail::concat("operation '", op, "' must have member 'value'");
                            break;
                        }
                        bool success = false;
                        JSON_TRY
                        {
                            success = (working.at(ptr) == *value_it);
                        }
                        JSON_INTERNAL_CATCH (const std::exception&)
                        {
                            success = false;
                        }

                        if (JSON_HEDLEY_UNLIKELY(!success))
                        {
                            op_message = detail::concat("unsuccessful: ", val.dump());
                            break;
                        }
                        op_ok = true;
                        break;
                    }

                    case patch_operations::invalid:
                    default:
                    {
                        op_message = detail::concat("operation value '", op, "' is invalid");
                        break;
                    }
                }
            }
            catch (const std::exception& e)
            {
                op_message = e.what();
            }
            catch (...)
            {
                op_message = "unknown error";
            }

            if (op_ok)
            {
                op_report.status = json_patch_preflight_status::ok;
            }
            else
            {
                op_report.status = json_patch_preflight_status::failed;
                op_report.message = op_message;
                blocked = true;
            }
            report.operations.push_back(op_report);
        }

        return report;
    }

  private:
    // wrapper for "add" operation; add value at ptr
    static void operation_add(BasicJsonType& result, json_pointer_t& ptr, const BasicJsonType& val)
    {
        // adding to the root of the target document means replacing it
        if (ptr.empty())
        {
            result = val;
            return;
        }

        // make sure the top element of the pointer exists
        json_pointer_t const top_pointer = ptr.top();
        if (top_pointer != ptr)
        {
            result.at(top_pointer);
        }

        // get reference to the parent of the JSON pointer ptr
        const auto last_path = ptr.back();
        ptr.pop_back();
        // parent must exist when performing patch add per RFC6902 specs
        BasicJsonType& parent = result.at(ptr);

        switch (parent.m_data.m_type)
        {
            case detail::value_t::null:
            case detail::value_t::object:
            {
                // use operator[] to add value
                parent[last_path] = val;
                break;
            }

            case detail::value_t::array:
            {
                if (last_path == "-")
                {
                    // special case: append to back
                    parent.push_back(val);
                }
                else
                {
                    const auto idx = json_pointer_t::template array_index<BasicJsonType>(last_path);
                    if (JSON_HEDLEY_UNLIKELY(idx > parent.size()))
                    {
                        // avoid undefined behavior
                        JSON_THROW(detail::out_of_range::create(401, detail::concat("array index ", std::to_string(idx), " is out of range"), &parent));
                    }

                    // default case: insert add offset
                    parent.insert(parent.begin() + static_cast<typename BasicJsonType::difference_type>(idx), val);
                }
                break;
            }

            // the parent of an "add" target must be an object or array
            // (see #4292)
            case detail::value_t::string:
            case detail::value_t::boolean:
            case detail::value_t::number_integer:
            case detail::value_t::number_unsigned:
            case detail::value_t::number_float:
            case detail::value_t::binary:
            case detail::value_t::discarded:
            default:
                JSON_THROW(detail::out_of_range::create(411, detail::concat("cannot add value: the JSON Patch 'add' target's parent is of type ", parent.type_name(), ", but must be an object or array"), &parent));
        }
    }

    // wrapper for "remove" operation; remove value at ptr
    static void operation_remove(BasicJsonType& result, json_pointer_t& ptr)
    {
        // get reference to the parent of the JSON pointer ptr
        const auto last_path = ptr.back();
        ptr.pop_back();
        BasicJsonType& parent = result.at(ptr);

        // remove child
        if (parent.is_object())
        {
            // perform range check
            auto it = parent.find(last_path);
            if (JSON_HEDLEY_LIKELY(it != parent.end()))
            {
                parent.erase(it);
            }
            else
            {
                JSON_THROW(detail::out_of_range::create(403, detail::concat("key '", last_path, "' not found"), &result));
            }
        }
        else if (parent.is_array())
        {
            // note erase performs range check
            parent.erase(json_pointer_t::template array_index<BasicJsonType>(last_path));
        }
        else
        {
            // the parent of a "remove" target must be an object or array
            // (see #5396)
            JSON_THROW(detail::out_of_range::create(413, detail::concat("cannot remove value: the JSON Patch 'remove' target's parent is of type ", parent.type_name(), ", but must be an object or array"), &parent));
        }
    }

    // RFC 6902 (section 4.4) forbids "from" from being a proper prefix of
    // "path" for a "move" operation: a location cannot be moved into one of
    // its own children.
    static bool is_proper_prefix(const json_pointer_t& from, const json_pointer_t& to)
    {
        const auto from_size = from.reference_tokens.size();
        if (from_size >= to.reference_tokens.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < from_size; ++i)
        {
            if (!(from.reference_tokens[i] == to.reference_tokens[i]))
            {
                return false;
            }
        }
        return true;
    }
};

NLOHMANN_JSON_NAMESPACE_END
