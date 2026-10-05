//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint> // uint8_t
#include <map> // map
#include <string> // string
#include <utility> // move
#include <vector> // vector

#include <nlohmann/detail/exceptions.hpp>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/value_t.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

/*!
@brief classification of a failure encountered while resolving a JSON pointer
       through a JSON document

@sa https://json.nlohmann.me/api/json_pointer_plan/
*/
enum class json_pointer_error_t : std::uint8_t
{
    success = 0,        ///< the pointer resolved to a value
    missing_key,        ///< an object did not contain the requested key
    wrong_container,    ///< a non-null primitive value was encountered where descent was required
    array_out_of_range, ///< an array index was out of range or the array index was "-"
    null_container,     ///< an existing null value was encountered where descent was required
    invalid_index       ///< an array index was malformed (parse error 106/109)
};

/*!
@brief an explicit, compiled plan to resolve several JSON pointers through one
       document

A JSON pointer plan is compiled from a list of @ref json_pointer values. The
compilation merges the common prefixes of the pointers into a trie, so that
each JSON value along a shared prefix is accessed only once when the plan is
executed. Execution returns one result per request, in request order; each
result is an independently owned copy of the pointed-to value or a classified
error.

@note Executing a plan never modifies the document it is executed against.

@sa https://json.nlohmann.me/api/json_pointer_plan/
*/
template<typename RefStringType>
class json_pointer_plan
{
  public:
    using string_t = RefStringType;
    using pointer_t = json_pointer<RefStringType>;

    /*!
    @brief the result of resolving a single request through a plan

    A result is either a successfully resolved value (an independently owned
    copy of the pointed-to JSON value) or a classified error. The boolean
    conversion returns whether the request succeeded.
    */
    template<typename BasicJsonType>
    struct result
    {
        /// the error classification; @ref json_pointer_error_t::success if the request succeeded
        json_pointer_error_t error = json_pointer_error_t::success;
        /// an independently owned copy of the pointed-to value; only valid if @ref error is @ref json_pointer_error_t::success
        BasicJsonType value{};
        /// the pointer prefix that could not be resolved; empty if the request succeeded
        pointer_t failed_prefix;

        /// @return whether the request resolved to a value
        constexpr bool ok() const noexcept
        {
            return error == json_pointer_error_t::success;
        }

        /// @return whether the request resolved to a value
        constexpr explicit operator bool() const noexcept
        {
            return ok();
        }
    };

    /// @brief create an empty plan
    json_pointer_plan() = default;

    /*!
    @brief compile a plan from a range of JSON pointers

    @param[in] first  the beginning of the pointer range
    @param[in] last   the end of the pointer range

    @return the compiled plan
    */
    template<typename InputIt>
    static json_pointer_plan compile(InputIt first, InputIt last)
    {
        json_pointer_plan plan;
        for (; first != last; ++first)
        {
            plan.add(*first);
        }
        return plan;
    }

    /*!
    @brief compile a plan from an initializer list of JSON pointers

    @param[in] pointers  the pointers to compile

    @return the compiled plan
    */
    static json_pointer_plan compile(std::initializer_list<pointer_t> pointers)
    {
        return compile(pointers.begin(), pointers.end());
    }

    /*!
    @brief execute the plan against a JSON document

    Each request is resolved through the document. Common prefixes are
    accessed only once. The returned vector contains one result per request,
    in request order; each result is an independently owned copy.

    @param[in] root  the document to resolve the pointers through

    @return one result per request, in request order
    */
    template<typename BasicJsonType>
    std::vector<result<BasicJsonType>> execute(const BasicJsonType& root) const
    {
        std::vector<result<BasicJsonType>> results(request_count_);
        execute_node(root, root_, pointer_t(), results);
        return results;
    }

    /// @return the number of requests in the plan
    std::size_t request_count() const noexcept
    {
        return request_count_;
    }

    /// @return whether the plan contains no requests
    bool empty() const noexcept
    {
        return request_count_ == 0;
    }

  private:
    friend class json_pointer<RefStringType>;

    /*!
    @brief a node in the compiled pointer trie

    A node corresponds to a prefix of the compiled pointers. The @a terminals
    are the request indices that end at this node (i.e. the prefix of this
    node is the whole pointer of those requests). The @a children map a single
    reference token to the node reached by descending through that token.
    */
    struct node
    {
        std::vector<std::size_t> terminals;
        std::map<string_t, node> children;
    };

    /*!
    @brief add a pointer to the plan

    @param[in] ptr  the pointer to add
    */
    void add(const pointer_t& ptr)
    {
        node* current = &root_;
        for (const auto& reference_token : ptr.reference_tokens)
        {
            current = &current->children[reference_token];
        }
        current->terminals.push_back(request_count_++);
    }

    /*!
    @brief resolve the requests in a trie node through a JSON value

    @param[in]     json_value   the JSON value at the prefix of this node
    @param[in]     trie_node    the trie node to resolve
    @param[in]     prefix       the pointer prefix of @a trie_node
    @param[in,out] results      the result vector to fill
    */
    template<typename BasicJsonType>
    void execute_node(const BasicJsonType& json_value, const node& trie_node,
                      const pointer_t& prefix, std::vector<result<BasicJsonType>>& results) const
    {
        // requests ending at this node point to json_value itself
        for (std::size_t req_idx : trie_node.terminals)
        {
            results[req_idx].error = json_pointer_error_t::success;
            results[req_idx].value = json_value;
            results[req_idx].failed_prefix = pointer_t();
        }

        // descend through each child token, accessing the JSON value once
        for (const auto& child : trie_node.children)
        {
            const string_t& token = child.first;
            const node& child_node = child.second;
            pointer_t child_prefix = prefix;
            child_prefix.push_back(token);

            switch (json_value.type())
            {
                case detail::value_t::object:
                {
                    // object lookup by string key; a numeric key is an object key, not an array index
                    const auto it = json_value.find(token);
                    if (JSON_HEDLEY_UNLIKELY(it == json_value.end()))
                    {
                        fail_subtree(child_node, json_pointer_error_t::missing_key, child_prefix, results);
                    }
                    else
                    {
                        execute_node(*it, child_node, child_prefix, results);
                    }
                    break;
                }

                case detail::value_t::array:
                {
                    if (JSON_HEDLEY_UNLIKELY(token == "-"))
                    {
                        // "-" always fails the range check
                        fail_subtree(child_node, json_pointer_error_t::array_out_of_range, prefix, results);
                        break;
                    }

                    typename BasicJsonType::size_type idx{};
                    JSON_TRY
                    {
                        idx = pointer_t::template array_index<BasicJsonType>(token);
                    }
                    JSON_INTERNAL_CATCH (detail::parse_error&)
                    {
                        // malformed array index (e.g. leading zero or non-digit)
                        fail_subtree(child_node, json_pointer_error_t::invalid_index, prefix, results);
                        break;
                    }
                    JSON_INTERNAL_CATCH (detail::out_of_range&)
                    {
                        // index cannot be represented as size_type
                        fail_subtree(child_node, json_pointer_error_t::array_out_of_range, prefix, results);
                        break;
                    }

                    if (JSON_HEDLEY_UNLIKELY(idx >= json_value.size()))
                    {
                        fail_subtree(child_node, json_pointer_error_t::array_out_of_range, prefix, results);
                    }
                    else
                    {
                        execute_node(json_value[static_cast<typename BasicJsonType::size_type>(idx)], child_node, child_prefix, results);
                    }
                    break;
                }

                case detail::value_t::null:
                {
                    // an existing null value cannot be descended into
                    fail_subtree(child_node, json_pointer_error_t::null_container, prefix, results);
                    break;
                }

                case detail::value_t::string:
                case detail::value_t::boolean:
                case detail::value_t::number_integer:
                case detail::value_t::number_unsigned:
                case detail::value_t::number_float:
                case detail::value_t::binary:
                case detail::value_t::discarded:
                default:
                {
                    // a primitive value cannot be descended into
                    fail_subtree(child_node, json_pointer_error_t::wrong_container, prefix, results);
                    break;
                }
            }
        }
    }

    /*!
    @brief mark all requests in a trie subtree as failed

    @param[in]     trie_node      the root of the subtree
    @param[in]     error          the error classification
    @param[in]     failed_prefix  the pointer prefix that could not be resolved
    @param[in,out] results        the result vector to fill
    */
    template<typename BasicJsonType>
    void fail_subtree(const node& trie_node, json_pointer_error_t error,
                      const pointer_t& failed_prefix, std::vector<result<BasicJsonType>>& results) const
    {
        for (std::size_t req_idx : trie_node.terminals)
        {
            results[req_idx].error = error;
            results[req_idx].value = BasicJsonType();
            results[req_idx].failed_prefix = failed_prefix;
        }

        // every request in this subtree failed at the same prefix, so keep
        // the failed_prefix unchanged while descending to find all terminals
        for (const auto& child : trie_node.children)
        {
            fail_subtree(child.second, error, failed_prefix, results);
        }
    }

    /// the root of the compiled pointer trie
    node root_;
    /// the number of requests in the plan
    std::size_t request_count_ = 0;
};

NLOHMANN_JSON_NAMESPACE_END
