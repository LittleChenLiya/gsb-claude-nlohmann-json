//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (three-way merge component)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013 - 2025 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#ifndef INCLUDE_NLOHMANN_JSON_MERGE_HPP_
#define INCLUDE_NLOHMANN_JSON_MERGE_HPP_

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

/// @brief three-way merge component for JSON values
NLOHMANN_JSON_NAMESPACE_BEGIN

/// @brief a single conflict found during a three-way merge
struct json_merge_conflict
{
    /// location of the conflicting value (empty string for the root value)
    json::json_pointer pointer;
    /// human-readable reason, e.g. "modified on both sides"
    std::string reason;
    /// the value in the common base (only meaningful if base_present)
    json base_value;
    /// the value in the local version (only meaningful if local_present)
    json local_value;
    /// the value in the incoming version (only meaningful if incoming_present)
    json incoming_value;
    /// whether the value exists in the common base
    bool base_present = false;
    /// whether the value exists in the local version
    bool local_present = false;
    /// whether the value exists in the incoming version
    bool incoming_present = false;
};

/// @brief options controlling the three-way merge
struct json_merge_options
{
    /// Maps the JSON Pointer of an array to the name of the object member
    /// that uniquely identifies array elements. Arrays at these pointers
    /// are merged element-wise by aligning on the id member; all other
    /// arrays are treated as opaque values.
    std::map<std::string, std::string> id_fields;
};

/// @brief the outcome of a three-way merge
struct json_merge_result
{
    /// the merged value; conflicting positions hold a deterministic
    /// fallback (the local value, or the incoming value if local deleted it)
    json merged;
    /// all conflicts, in deterministic (sorted-pointer) traversal order
    std::vector<json_merge_conflict> conflicts;

    /// whether the merge completed without conflicts
    bool success() const
    {
        return conflicts.empty();
    }
};

namespace detail
{

class json_three_way_merger
{
  public:
    explicit json_three_way_merger(const json_merge_options& options)
        : opts(options)
    {}

    json_merge_result merge(const json& base, const json& local, const json& incoming)
    {
        json_merge_result result;
        json merged;
        if (merge_value(json::json_pointer(), &base, &local, &incoming, merged, result))
        {
            result.merged = std::move(merged);
        }
        else
        {
            // both sides deleted the root value; represent as null
            result.merged = nullptr;
        }
        return result;
    }

  private:
    static bool same(const json* lhs, const json* rhs)
    {
        if (lhs == nullptr || rhs == nullptr)
        {
            return lhs == rhs;
        }
        return *lhs == *rhs;
    }

    static const json* find(const json* object, const std::string& key)
    {
        if (object == nullptr || !object->is_object())
        {
            return nullptr;
        }
        const auto it = object->find(key);
        return it == object->end() ? nullptr : &(*it);
    }

    void record(const json::json_pointer& ptr, std::string reason,
                const json* base, const json* local, const json* incoming,
                json_merge_result& result) const
    {
        json_merge_conflict conflict;
        conflict.pointer = ptr;
        conflict.reason = std::move(reason);
        conflict.base_present = base != nullptr;
        conflict.local_present = local != nullptr;
        conflict.incoming_present = incoming != nullptr;
        if (base != nullptr)
        {
            conflict.base_value = *base;
        }
        if (local != nullptr)
        {
            conflict.local_value = *local;
        }
        if (incoming != nullptr)
        {
            conflict.incoming_value = *incoming;
        }
        result.conflicts.push_back(std::move(conflict));
    }

    // Returns true and sets out if the merged value exists; returns false
    // if the value is absent (deleted) in the merged result.
    bool merge_value(const json::json_pointer& ptr,
                     const json* base, const json* local, const json* incoming,
                     json& out, json_merge_result& result)
    {
        // both sides agree (includes both-absent and same-value edits)
        if (same(local, incoming))
        {
            if (local != nullptr)
            {
                out = *local;
                return true;
            }
            return false;
        }
        // only incoming changed
        if (same(base, local))
        {
            if (incoming != nullptr)
            {
                out = *incoming;
                return true;
            }
            return false;
        }
        // only local changed
        if (same(base, incoming))
        {
            if (local != nullptr)
            {
                out = *local;
                return true;
            }
            return false;
        }

        // both sides changed the value in different ways
        if (base != nullptr && local != nullptr && incoming != nullptr
                && base->is_object() && local->is_object() && incoming->is_object())
        {
            merge_objects(ptr, *base, *local, *incoming, out, result);
            return true;
        }

        if (base != nullptr && local != nullptr && incoming != nullptr
                && base->is_array() && local->is_array() && incoming->is_array())
        {
            const auto id_field = opts.id_fields.find(ptr.to_string());
            if (id_field != opts.id_fields.end())
            {
                merge_arrays_by_id(ptr, *base, *local, *incoming, id_field->second, out, result);
                return true;
            }
        }

        // unresolvable conflict; keep a deterministic fallback value
        record(ptr,
               (local == nullptr || incoming == nullptr)
               ? "deleted on one side, modified on the other"
               : "modified on both sides",
               base, local, incoming, result);
        if (local != nullptr)
        {
            out = *local;
            return true;
        }
        if (incoming != nullptr)
        {
            out = *incoming;
            return true;
        }
        return false;
    }

    void merge_objects(const json::json_pointer& ptr,
                       const json& base, const json& local, const json& incoming,
                       json& out, json_merge_result& result)
    {
        out = json::object();
        std::set<std::string> keys;
        for (const auto* object : {&base, &local, &incoming})
        {
            for (auto it = object->begin(); it != object->end(); ++it)
            {
                keys.insert(it.key());
            }
        }
        for (const auto& key : keys)
        {
            json merged_child;
            if (merge_value(ptr / key, find(&base, key), find(&local, key), find(&incoming, key),
                            merged_child, result))
            {
                out[key] = std::move(merged_child);
            }
        }
    }

    static bool element_id(const json& element, const std::string& id_field, std::string& id)
    {
        if (!element.is_object())
        {
            return false;
        }
        const auto it = element.find(id_field);
        if (it == element.end())
        {
            return false;
        }
        if (it->is_string())
        {
            id = it->template get<std::string>();
        }
        else
        {
            id = it->dump();
        }
        return true;
    }

    // Collects element ids in array order. Returns false (after recording a
    // conflict) if an element has no usable id or an id occurs twice.
    bool collect_ids(const json::json_pointer& ptr, const json& array,
                     const std::string& id_field,
                     std::vector<std::string>& order,
                     std::map<std::string, const json*>& by_id,
                     json_merge_result& result) const
    {
        bool ok = true;
        for (const auto& element : array)
        {
            std::string id;
            if (!element_id(element, id_field, id))
            {
                record(ptr, "array element without unique id", &array, nullptr, nullptr, result);
                return false;
            }
            if (by_id.find(id) != by_id.end())
            {
                record(ptr / id, "duplicate id in array", &element, nullptr, nullptr, result);
                ok = false;
                continue; // keep the first occurrence
            }
            by_id[id] = &element;
            order.push_back(id);
        }
        return ok;
    }

    void merge_arrays_by_id(const json::json_pointer& ptr,
                            const json& base, const json& local, const json& incoming,
                            const std::string& id_field,
                            json& out, json_merge_result& result)
    {
        std::vector<std::string> base_order, local_order, incoming_order;
        std::map<std::string, const json*> base_by_id, local_by_id, incoming_by_id;
        const bool ok_base = collect_ids(ptr, base, id_field, base_order, base_by_id, result);
        const bool ok_local = collect_ids(ptr, local, id_field, local_order, local_by_id, result);
        const bool ok_incoming = collect_ids(ptr, incoming, id_field, incoming_order, incoming_by_id, result);
        if (!ok_base || !ok_local || !ok_incoming)
        {
            // cannot align elements; fall back to the local array
            out = local;
            return;
        }

        out = json::array();

        // elements known to the base keep their base order
        for (std::size_t idx = 0; idx < base_order.size(); ++idx)
        {
            const std::string& id = base_order[idx];
            const json* base_element = base_by_id[id];
            const json* local_element = find(local_by_id, id);
            const json* incoming_element = find(incoming_by_id, id);
            json merged_element;
            if (merge_value(ptr / idx, base_element, local_element, incoming_element,
                            merged_element, result))
            {
                out.push_back(std::move(merged_element));
            }
        }

        // new elements are appended: first the ones added locally ...
        for (const auto& id : local_order)
        {
            if (base_by_id.find(id) != base_by_id.end())
            {
                continue;
            }
            const json* local_element = local_by_id[id];
            const json* incoming_element = find(incoming_by_id, id);
            if (incoming_element == nullptr || *incoming_element == *local_element)
            {
                out.push_back(*local_element);
            }
            else
            {
                record(ptr / id, "added on both sides with different values",
                       nullptr, local_element, incoming_element, result);
                out.push_back(*local_element);
            }
        }

        // ... then the ones added by the incoming side
        for (const auto& id : incoming_order)
        {
            if (base_by_id.find(id) != base_by_id.end()
                    || local_by_id.find(id) != local_by_id.end())
            {
                continue;
            }
            out.push_back(*incoming_by_id[id]);
        }
    }

    static const json* find(const std::map<std::string, const json*>& by_id, const std::string& id)
    {
        const auto it = by_id.find(id);
        return it == by_id.end() ? nullptr : it->second;
    }

    json_merge_options opts;
};

} // namespace detail

/// @brief merges two JSON values derived from a common base
///
/// Objects are merged recursively key by key. Arrays are treated as opaque
/// values unless their JSON Pointer is registered in
/// json_merge_options::id_fields, in which case elements are aligned by
/// their unique id member: unchanged elements keep their base order and new
/// elements are appended, local additions first, then incoming ones.
/// Conflicting changes are reported in the result in deterministic order.
inline json_merge_result three_way_merge(const json& base, const json& local, const json& incoming,
        const json_merge_options& options = json_merge_options())
{
    detail::json_three_way_merger merger(options);
    return merger.merge(base, local, incoming);
}

NLOHMANN_JSON_NAMESPACE_END

#endif // INCLUDE_NLOHMANN_JSON_MERGE_HPP_
