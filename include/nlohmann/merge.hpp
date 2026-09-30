//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
//  |  |  |__   |  |  | | | |  version 3.12.0
//  |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
//  SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
//  SPDX-License-Identifier: MIT

#ifndef INCLUDE_NLOHMANN_MERGE_HPP_
#define INCLUDE_NLOHMANN_MERGE_HPP_

#include <algorithm> // find
#include <map> // map
#include <string> // string
#include <vector> // vector

#include <nlohmann/json.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

/// @brief the kind of a conflict detected during a three-way merge
enum class merge_conflict_kind
{
    delete_vs_modify, ///< one side deleted a value the other side modified
    modify_vs_modify, ///< both sides changed the same value to different results
    add_vs_add,       ///< both sides added the same key/id with different values
    duplicate_id      ///< an id-addressed array contains the same id more than once
};

/// @brief a single conflict detected by merge3()
struct merge_conflict
{
    json::json_pointer pointer; ///< location of the conflict; inside id-addressed arrays the id is used as the final reference token
    merge_conflict_kind kind;   ///< what kind of conflict occurred
    bool local_deleted;         ///< whether the local side deleted the value
    bool incoming_deleted;      ///< whether the incoming side deleted the value
    json local;                 ///< the value the local side wanted (null if it deleted the value)
    json incoming;              ///< the value the incoming side wanted (null if it deleted the value)
};

/// @brief options for merge3()
struct merge_options
{
    /// @brief maps a JSON pointer addressing an array to the name of the object
    /// member that uniquely identifies the elements of that array
    std::map<json::json_pointer, std::string> id_fields {};
};

/// @brief the result of merge3()
struct merge_result
{
    json merged = nullptr;                  ///< the merged JSON value
    std::vector<merge_conflict> conflicts {}; ///< all detected conflicts in stable document order

    /// @brief whether any conflict was detected
    bool has_conflicts() const noexcept
    {
        return !conflicts.empty();
    }
};

namespace detail
{

inline const std::string* merge3_id_field(const merge_options& options, const json::json_pointer& pointer)
{
    const auto it = options.id_fields.find(pointer);
    return it == options.id_fields.end() ? nullptr : &(it->second);
}

struct merge3_id_table
{
    std::vector<std::string> order;              // id keys in first-occurrence order
    std::map<std::string, std::string> tokens;   // id key -> JSON pointer reference token
    std::map<std::string, const json*> elements; // id key -> first element with that id
    std::vector<std::string> duplicates;         // id keys occurring more than once
};

inline bool merge3_id_key(const json& element, const std::string& field, std::string& key, std::string& token)
{
    if (!element.is_object())
    {
        return false;
    }
    const auto it = element.find(field);
    if (it == element.end() || it->is_null() || it->is_structured())
    {
        return false;
    }
    key = it->dump();
    token = it->is_string() ? it->get<std::string>() : key;
    return true;
}

inline merge3_id_table merge3_index_array(const json& array, const std::string& field, bool& ok)
{
    merge3_id_table table;
    ok = true;
    for (const auto& element : array)
    {
        std::string key;
        std::string token;
        if (!merge3_id_key(element, field, key, token))
        {
            ok = false;
            return table;
        }
        if (table.elements.find(key) != table.elements.end())
        {
            if (std::find(table.duplicates.begin(), table.duplicates.end(), key) == table.duplicates.end())
            {
                table.duplicates.push_back(key);
            }
            continue; // keep the first occurrence
        }
        table.order.push_back(key);
        table.tokens.emplace(key, token);
        table.elements.emplace(key, &element);
    }
    return table;
}

inline json merge3_impl(const json& base, const json& local, const json& incoming,
                        const merge_options& options, const json::json_pointer& pointer,
                        std::vector<merge_conflict>& conflicts);

inline json merge3_objects(const json& base, const json& local, const json& incoming,
                           const merge_options& options, const json::json_pointer& pointer,
                           std::vector<merge_conflict>& conflicts)
{
    // key order: base keys first, then keys added by local, then keys added by incoming
    std::vector<std::string> keys;
    for (const json* object : {&base, &local, &incoming})
    {
        for (auto it = object->begin(); it != object->end(); ++it)
        {
            if (std::find(keys.begin(), keys.end(), it.key()) == keys.end())
            {
                keys.push_back(it.key());
            }
        }
    }

    json result = json::object();
    for (const auto& key : keys)
    {
        json::json_pointer child = pointer;
        child /= key;

        const auto base_it = base.find(key);
        const auto local_it = local.find(key);
        const auto incoming_it = incoming.find(key);
        const bool in_base = base_it != base.end();
        const bool in_local = local_it != local.end();
        const bool in_incoming = incoming_it != incoming.end();

        if (in_base)
        {
            if (!in_local && !in_incoming)
            {
                continue; // deleted on both sides
            }
            if (!in_local || !in_incoming)
            {
                const json& kept = in_local ? *local_it : *incoming_it;
                if (kept == *base_it)
                {
                    continue; // deleted on one side, unchanged on the other
                }
                conflicts.push_back(merge_conflict{child, merge_conflict_kind::delete_vs_modify,
                                                   !in_local, !in_incoming,
                                                   in_local ? *local_it : json(nullptr),
                                                   in_incoming ? *incoming_it : json(nullptr)});
                continue; // the deletion wins
            }
            result[key] = merge3_impl(*base_it, *local_it, *incoming_it, options, child, conflicts);
        }
        else if (in_local && in_incoming)
        {
            if (*local_it == *incoming_it)
            {
                result[key] = *local_it; // same value added on both sides
            }
            else
            {
                conflicts.push_back(merge_conflict{child, merge_conflict_kind::add_vs_add,
                                                   false, false, *local_it, *incoming_it});
                result[key] = *local_it; // local wins
            }
        }
        else if (in_local)
        {
            result[key] = *local_it;
        }
        else
        {
            result[key] = *incoming_it;
        }
    }
    return result;
}

inline json merge3_id_arrays(const json& base, const json& local, const json& incoming,
                             const std::string& field, const merge_options& options,
                             const json::json_pointer& pointer,
                             std::vector<merge_conflict>& conflicts, bool& handled)
{
    bool ok_base = false;
    bool ok_local = false;
    bool ok_incoming = false;
    const merge3_id_table base_table = merge3_index_array(base, field, ok_base);
    const merge3_id_table local_table = merge3_index_array(local, field, ok_local);
    const merge3_id_table incoming_table = merge3_index_array(incoming, field, ok_incoming);
    if (!ok_base || !ok_local || !ok_incoming)
    {
        handled = false; // elements without a usable id: treat the array as an atomic value
        return json(nullptr);
    }
    handled = true;

    // report duplicate ids in stable order: base, local, incoming
    const merge3_id_table* tables[3] = {&base_table, &local_table, &incoming_table};
    for (std::size_t side = 0; side < 3; ++side)
    {
        for (const auto& key : tables[side]->duplicates)
        {
            json::json_pointer child = pointer;
            child /= tables[side]->tokens.at(key);
            const json& element = *(tables[side]->elements.at(key));
            conflicts.push_back(merge_conflict{child, merge_conflict_kind::duplicate_id,
                                               false, false,
                                               side == 1 ? element : json(nullptr),
                                               side == 2 ? element : json(nullptr)});
        }
    }

    // element order: surviving base elements in base order, then elements added
    // by local in local order, then elements added by incoming in incoming order
    std::vector<std::string> ids;
    for (const merge3_id_table* table : tables)
    {
        for (const auto& key : table->order)
        {
            if (std::find(ids.begin(), ids.end(), key) == ids.end())
            {
                ids.push_back(key);
            }
        }
    }

    json result = json::array();
    for (const auto& id : ids)
    {
        std::string token;
        for (const merge3_id_table* table : tables)
        {
            const auto tok = table->tokens.find(id);
            if (tok != table->tokens.end())
            {
                token = tok->second;
                break;
            }
        }
        json::json_pointer child = pointer;
        child /= token;

        const auto base_el = base_table.elements.find(id);
        const auto local_el = local_table.elements.find(id);
        const auto incoming_el = incoming_table.elements.find(id);
        const bool in_base = base_el != base_table.elements.end();
        const bool in_local = local_el != local_table.elements.end();
        const bool in_incoming = incoming_el != incoming_table.elements.end();

        if (in_base)
        {
            if (!in_local && !in_incoming)
            {
                continue; // deleted on both sides
            }
            if (!in_local || !in_incoming)
            {
                const json& kept = in_local ? *(local_el->second) : *(incoming_el->second);
                if (kept == *(base_el->second))
                {
                    continue; // deleted on one side, unchanged on the other
                }
                conflicts.push_back(merge_conflict{child, merge_conflict_kind::delete_vs_modify,
                                                   !in_local, !in_incoming,
                                                   in_local ? *(local_el->second) : json(nullptr),
                                                   in_incoming ? *(incoming_el->second) : json(nullptr)});
                continue; // the deletion wins
            }
            result.push_back(merge3_impl(*(base_el->second), *(local_el->second), *(incoming_el->second),
                                         options, child, conflicts));
        }
        else if (in_local && in_incoming)
        {
            if (*(local_el->second) == *(incoming_el->second))
            {
                result.push_back(*(local_el->second)); // same element added on both sides
            }
            else
            {
                conflicts.push_back(merge_conflict{child, merge_conflict_kind::add_vs_add,
                                                   false, false, *(local_el->second), *(incoming_el->second)});
                result.push_back(*(local_el->second)); // local wins
            }
        }
        else if (in_local)
        {
            result.push_back(*(local_el->second));
        }
        else
        {
            result.push_back(*(incoming_el->second));
        }
    }
    return result;
}

inline json merge3_impl(const json& base, const json& local, const json& incoming,
                        const merge_options& options, const json::json_pointer& pointer,
                        std::vector<merge_conflict>& conflicts)
{
    // arrays registered in merge_options::id_fields are always aligned by id,
    // even if one side is unchanged, so that duplicate ids are still detected
    if (base.is_array() && local.is_array() && incoming.is_array())
    {
        const std::string* field = merge3_id_field(options, pointer);
        if (field != nullptr)
        {
            bool handled = false;
            json merged = merge3_id_arrays(base, local, incoming, *field, options, pointer, conflicts, handled);
            if (handled)
            {
                return merged;
            }
        }
    }

    // a side that did not change the value defers to the other side
    if (local == base)
    {
        return incoming;
    }
    if (incoming == base)
    {
        return local;
    }
    if (local == incoming)
    {
        return local; // both sides made the same change
    }

    if (base.is_object() && local.is_object() && incoming.is_object())
    {
        return merge3_objects(base, local, incoming, options, pointer, conflicts);
    }

    // plain arrays, scalars, and type changes are merged atomically: both sides
    // changed the same value to different results, so local wins deterministically
    conflicts.push_back(merge_conflict{pointer, merge_conflict_kind::modify_vs_modify,
                                       false, false, local, incoming});
    return local;
}

} // namespace detail

/// @brief merges two JSON values derived from a common base
///
/// Performs a three-way merge of @a local and @a incoming against their common
/// ancestor @a base. Objects are merged recursively key by key. Arrays are
/// treated as atomic values unless their location is registered in
/// @ref merge_options::id_fields, in which case their elements are aligned by
/// the declared unique id member: surviving elements keep their base order and
/// new elements are appended in local-then-incoming order. Non-conflicting
/// changes are merged automatically; conflicting changes are resolved in favor
/// of @a local (deletions included) and reported in @ref merge_result::conflicts
/// in stable document order.
inline merge_result merge3(const json& base, const json& local, const json& incoming,
                           const merge_options& options = merge_options())
{
    merge_result result;
    result.merged = detail::merge3_impl(base, local, incoming, options, json::json_pointer(), result.conflicts);
    return result;
}

NLOHMANN_JSON_NAMESPACE_END

#endif // INCLUDE_NLOHMANN_MERGE_HPP_
