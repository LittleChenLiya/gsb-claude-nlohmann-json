//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm> // all_of
#include <cctype> // isdigit
#include <cerrno> // errno, ERANGE
#include <cstdlib> // strtoull
#ifndef JSON_NO_IO
    #include <iosfwd> // ostream
#endif  // JSON_NO_IO
#include <limits> // max
#include <map> // map
#include <memory> // shared_ptr
#include <numeric> // accumulate
#include <set> // set
#include <string> // string
#include <utility> // move, pair
#include <vector> // vector

#include <nlohmann/detail/exceptions.hpp>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/string_concat.hpp>
#include <nlohmann/detail/string_escape.hpp>
#include <nlohmann/detail/value_t.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

template<typename BasicJsonType>
class json_pointer_plan;

/// @brief JSON Pointer defines a string syntax for identifying a specific value within a JSON document
/// @sa https://json.nlohmann.me/api/json_pointer/
template<typename RefStringType>
class json_pointer
{
    // allow basic_json to access private members
    NLOHMANN_BASIC_JSON_TPL_DECLARATION
    friend class basic_json;

    // allow json_pointer_plan to tokenize pointers when compiling a plan
    template<typename>
    friend class json_pointer_plan;

    template<typename>
    friend class json_pointer;

    template<typename T>
    struct string_t_helper
    {
        using type = T;
    };

    NLOHMANN_BASIC_JSON_TPL_DECLARATION
    struct string_t_helper<NLOHMANN_BASIC_JSON_TPL>
    {
        using type = StringType;
    };

  public:
    // for backwards compatibility accept BasicJsonType
    using string_t = typename string_t_helper<RefStringType>::type;

    /// @brief create JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/json_pointer/
    explicit json_pointer(const string_t& s = "")
        : reference_tokens(split(s))
    {}

    /// @brief return a string representation of the JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/to_string/
    string_t to_string() const
    {
        return std::accumulate(reference_tokens.begin(), reference_tokens.end(),
                               string_t{},
                               [](const string_t& a, const string_t& b)
        {
            return detail::concat<string_t>(a, '/', detail::escape(b));
        });
    }

    /// @brief return a string representation of the JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_string/
    JSON_HEDLEY_DEPRECATED_FOR(3.11.0, to_string())
    operator string_t() const
    {
        return to_string();
    }

#ifndef JSON_NO_IO
    /// @brief write string representation of the JSON pointer to stream
    /// @sa https://json.nlohmann.me/api/basic_json/operator_ltlt/
    friend std::ostream& operator<<(std::ostream& o, const json_pointer& ptr)
    {
        o << ptr.to_string();
        return o;
    }
#endif

    /// @brief append another JSON pointer at the end of this JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_slasheq/
    json_pointer& operator/=(const json_pointer& ptr)
    {
        reference_tokens.insert(reference_tokens.end(),
                                ptr.reference_tokens.begin(),
                                ptr.reference_tokens.end());
        return *this;
    }

    /// @brief append an unescaped reference token at the end of this JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_slasheq/
    json_pointer& operator/=(string_t token)
    {
        push_back(std::move(token));
        return *this;
    }

    /// @brief append an array index at the end of this JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_slasheq/
    json_pointer& operator/=(std::size_t array_idx)
    {
        return *this /= std::to_string(array_idx);
    }

    /// @brief create a new JSON pointer by appending the right JSON pointer at the end of the left JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_slash/
    friend json_pointer operator/(const json_pointer& lhs,
                                  const json_pointer& rhs)
    {
        return json_pointer(lhs) /= rhs;
    }

    /// @brief create a new JSON pointer by appending the unescaped token at the end of the JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_slash/
    friend json_pointer operator/(const json_pointer& lhs, string_t token) // NOLINT(performance-unnecessary-value-param)
    {
        return json_pointer(lhs) /= std::move(token);
    }

    /// @brief create a new JSON pointer by appending the array-index-token at the end of the JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_slash/
    friend json_pointer operator/(const json_pointer& lhs, std::size_t array_idx)
    {
        return json_pointer(lhs) /= array_idx;
    }

    /// @brief returns the parent of this JSON pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/parent_pointer/
    json_pointer parent_pointer() const
    {
        if (empty())
        {
            return *this;
        }

        json_pointer res = *this;
        res.pop_back();
        return res;
    }

    /// @brief remove first reference token
    /// @sa https://json.nlohmann.me/api/json_pointer/pop_front/
    void pop_front()
    {
        if (JSON_HEDLEY_UNLIKELY(empty()))
        {
            JSON_THROW(detail::out_of_range::create(405, "JSON pointer has no parent", nullptr));
        }

        reference_tokens.erase(reference_tokens.begin());
    }

    /// @brief return first reference token
    /// @sa https://json.nlohmann.me/api/json_pointer/front/
    const string_t& front() const
    {
        if (JSON_HEDLEY_UNLIKELY(empty()))
        {
            JSON_THROW(detail::out_of_range::create(405, "JSON pointer has no parent", nullptr));
        }

        return reference_tokens.front();
    }

    /// @brief append an unescaped token at the start of the reference pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/push_front/
    void push_front(const string_t& token)
    {
        reference_tokens.insert(reference_tokens.begin(), token);
    }

    /// @brief append an unescaped token at the start of the reference pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/push_front/
    void push_front(string_t&& token)
    {
        reference_tokens.insert(reference_tokens.begin(), std::move(token));
    }

    /// @brief remove last reference token
    /// @sa https://json.nlohmann.me/api/json_pointer/pop_back/
    void pop_back()
    {
        if (JSON_HEDLEY_UNLIKELY(empty()))
        {
            JSON_THROW(detail::out_of_range::create(405, "JSON pointer has no parent", nullptr));
        }

        reference_tokens.pop_back();
    }

    /// @brief return last reference token
    /// @sa https://json.nlohmann.me/api/json_pointer/back/
    const string_t& back() const
    {
        if (JSON_HEDLEY_UNLIKELY(empty()))
        {
            JSON_THROW(detail::out_of_range::create(405, "JSON pointer has no parent", nullptr));
        }

        return reference_tokens.back();
    }

    /// @brief append an unescaped token at the end of the reference pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/push_back/
    void push_back(const string_t& token)
    {
        reference_tokens.push_back(token);
    }

    /// @brief append an unescaped token at the end of the reference pointer
    /// @sa https://json.nlohmann.me/api/json_pointer/push_back/
    void push_back(string_t&& token)
    {
        reference_tokens.push_back(std::move(token));
    }

    /// @brief return whether pointer points to the root document
    /// @sa https://json.nlohmann.me/api/json_pointer/empty/
    bool empty() const noexcept
    {
        return reference_tokens.empty();
    }

  private:
    /*!
    @param[in] s  reference token to be converted into an array index

    @return integer representation of @a s

    @throw parse_error.106  if an array index begins with '0'
    @throw parse_error.109  if an array index begins not with a digit
    @throw out_of_range.404 if string @a s could not be converted to an integer
    @throw out_of_range.410 if an array index exceeds size_type
    */
    template<typename BasicJsonType>
    static typename BasicJsonType::size_type array_index(const string_t& s)
    {
        using size_type = typename BasicJsonType::size_type;

        // error condition (cf. RFC 6901, Sect. 4)
        if (JSON_HEDLEY_UNLIKELY(s.size() > 1 && s[0] == '0'))
        {
            JSON_THROW(detail::parse_error::create(106, 0, detail::concat("array index '", s, "' must not begin with '0'"), nullptr));
        }

        // error condition (cf. RFC 6901, Sect. 4)
        if (JSON_HEDLEY_UNLIKELY(s.size() > 1 && !(s[0] >= '1' && s[0] <= '9')))
        {
            JSON_THROW(detail::parse_error::create(109, 0, detail::concat("array index '", s, "' is not a number"), nullptr));
        }

        const char* p = s.data();
        char* p_end = nullptr; // NOLINT(misc-const-correctness)
        errno = 0; // strtoull doesn't reset errno
        const unsigned long long res = std::strtoull(p, &p_end, 10); // NOLINT(runtime/int)
        if (p == p_end // invalid input or empty string
                || errno == ERANGE // out of range
                || JSON_HEDLEY_UNLIKELY(static_cast<std::size_t>(p_end - p) != s.size())) // incomplete read
        {
            JSON_THROW(detail::out_of_range::create(404, detail::concat("unresolved reference token '", s, "'"), nullptr));
        }

        // only triggered on special platforms (like 32bit), see also
        // https://github.com/nlohmann/json/pull/2203
        if (res >= static_cast<unsigned long long>((std::numeric_limits<size_type>::max)()))  // NOLINT(runtime/int)
        {
            JSON_THROW(detail::out_of_range::create(410, detail::concat("array index ", s, " exceeds size_type"), nullptr));   // LCOV_EXCL_LINE
        }

        return static_cast<size_type>(res);
    }

  JSON_PRIVATE_UNLESS_TESTED:
    json_pointer top() const
    {
        if (JSON_HEDLEY_UNLIKELY(empty()))
        {
            JSON_THROW(detail::out_of_range::create(405, "JSON pointer has no parent", nullptr));
        }

        json_pointer result = *this;
        result.reference_tokens = {reference_tokens[0]};
        return result;
    }

  private:
    /*!
    @brief the reference token sequences that denote arrays

    @ref unflatten collects the pointer prefixes that have a reference token 0
    among their children; @ref get_and_create creates arrays exactly below
    those prefixes and objects everywhere else. Deciding this up front keeps
    the result independent of the order in which the flattened object is
    iterated, which is unspecified for some object types.
    */
    using array_parents_t = std::set<std::vector<string_t>>;

    /*!
    @brief create and return a reference to the pointed to value

    @complexity Linear in the number of reference tokens.

    @throw parse_error.106 if an array index begins with '0'
    @throw parse_error.109 if array index is not a number
    @throw type_error.313 if value cannot be unflattened
    */
    template<typename BasicJsonType>
    BasicJsonType& get_and_create(BasicJsonType& j, const array_parents_t& array_parents) const
    {
        auto* result = &j;

        // the reference tokens that have been consumed so far; used to look up
        // whether the value to be created below is an array or an object
        std::vector<string_t> prefix;

        // in case no reference tokens exist, return a reference to the JSON value
        // j which will be overwritten by a primitive value
        for (const auto& reference_token : reference_tokens)
        {
            switch (result->type())
            {
                case detail::value_t::null:
                {
                    if (array_parents.find(prefix) != array_parents.end())
                    {
                        // some reference token below this position is 0, so the
                        // value is an array
                        result = &result->operator[](array_index<BasicJsonType>(reference_token));
                    }
                    else
                    {
                        // start a new object otherwise
                        result = &result->operator[](reference_token);
                    }
                    break;
                }

                case detail::value_t::object:
                {
                    // create an entry in the object
                    result = &result->operator[](reference_token);
                    break;
                }

                case detail::value_t::array:
                {
                    // create an entry in the array
                    result = &result->operator[](array_index<BasicJsonType>(reference_token));
                    break;
                }

                /*
                The following code is only reached if there exists a reference
                token _and_ the current value is primitive. In this case, we have
                an error situation, because primitive values may only occur as
                a single value; that is, with an empty list of reference tokens.
                */
                case detail::value_t::string:
                case detail::value_t::boolean:
                case detail::value_t::number_integer:
                case detail::value_t::number_unsigned:
                case detail::value_t::number_float:
                case detail::value_t::binary:
                case detail::value_t::discarded:
                default:
                    JSON_THROW(detail::type_error::create(313, "invalid value to unflatten", &j));
            }

            prefix.push_back(reference_token);
        }

        return *result;
    }

    /*!
    @brief return a reference to the pointed to value

    @note This version does not throw if a value is not present, but tries to
          create nested values instead. For instance, calling this function
          with pointer `"/this/that"` on a null value is equivalent to calling
          `operator[]("this").operator[]("that")` on that value, effectively
          changing the null value to an object.

    @param[in] ptr  a JSON value

    @return reference to the JSON value pointed to by the JSON pointer

    @complexity Linear in the length of the JSON pointer.

    @throw parse_error.106   if an array index begins with '0'
    @throw parse_error.109   if an array index was not a number
    @throw out_of_range.404  if the JSON pointer can not be resolved
    */
    template<typename BasicJsonType>
    BasicJsonType& get_unchecked(BasicJsonType* ptr) const
    {
        for (const auto& reference_token : reference_tokens)
        {
            // convert null values to arrays or objects before continuing
            if (ptr->is_null())
            {
                // check if the reference token is a number
                const bool nums =
                    std::all_of(reference_token.begin(), reference_token.end(),
                                [](const unsigned char x)
                {
                    return std::isdigit(x);
                });

                // change value to an array for numbers or "-" or to object otherwise
                *ptr = (nums || reference_token == "-")
                       ? detail::value_t::array
                       : detail::value_t::object;
            }

            switch (ptr->type())
            {
                case detail::value_t::object:
                {
                    // use unchecked object access
                    ptr = &ptr->operator[](reference_token);
                    break;
                }

                case detail::value_t::array:
                {
                    if (reference_token == "-")
                    {
                        // explicitly treat "-" as index beyond the end
                        ptr = &ptr->operator[](ptr->m_data.m_value.array->size());
                    }
                    else
                    {
                        // convert array index to number; unchecked access
                        ptr = &ptr->operator[](array_index<BasicJsonType>(reference_token));
                    }
                    break;
                }

                case detail::value_t::null:
                case detail::value_t::string:
                case detail::value_t::boolean:
                case detail::value_t::number_integer:
                case detail::value_t::number_unsigned:
                case detail::value_t::number_float:
                case detail::value_t::binary:
                case detail::value_t::discarded:
                default:
                    JSON_THROW(detail::out_of_range::create(404, detail::concat("unresolved reference token '", reference_token, "'"), ptr));
            }
        }

        return *ptr;
    }

    /*!
    @throw parse_error.106   if an array index begins with '0'
    @throw parse_error.109   if an array index was not a number
    @throw out_of_range.402  if the array index '-' is used
    @throw out_of_range.404  if the JSON pointer can not be resolved
    */
    template<typename BasicJsonType>
    BasicJsonType& get_checked(BasicJsonType* ptr) const
    {
        for (const auto& reference_token : reference_tokens)
        {
            switch (ptr->type())
            {
                case detail::value_t::object:
                {
                    // note: at performs range check
                    ptr = &ptr->at(reference_token);
                    break;
                }

                case detail::value_t::array:
                {
                    if (JSON_HEDLEY_UNLIKELY(reference_token == "-"))
                    {
                        // "-" always fails the range check
                        JSON_THROW(detail::out_of_range::create(402, detail::concat(
                                "array index '-' (", std::to_string(ptr->m_data.m_value.array->size()),
                                ") is out of range"), ptr));
                    }

                    const auto idx = array_index<BasicJsonType>(reference_token);
                    // Bounds check before access to avoid exception with JSON_NOEXCEPTION
                    if (JSON_HEDLEY_UNLIKELY(idx >= ptr->m_data.m_value.array->size()))
                    {
                        JSON_THROW(detail::out_of_range::create(401, detail::concat(
                                "array index ", std::to_string(idx), " is out of range"), ptr));
                    }
                    ptr = &ptr->operator[](idx);
                    break;
                }

                case detail::value_t::null:
                case detail::value_t::string:
                case detail::value_t::boolean:
                case detail::value_t::number_integer:
                case detail::value_t::number_unsigned:
                case detail::value_t::number_float:
                case detail::value_t::binary:
                case detail::value_t::discarded:
                default:
                    JSON_THROW(detail::out_of_range::create(404, detail::concat("unresolved reference token '", reference_token, "'"), ptr));
            }
        }

        return *ptr;
    }

    /*!
    @brief return a const reference to the pointed to value

    @param[in] ptr  a JSON value

    @return const reference to the JSON value pointed to by the JSON
    pointer

    @throw parse_error.106   if an array index begins with '0'
    @throw parse_error.109   if an array index was not a number
    @throw out_of_range.402  if the array index '-' is used
    @throw out_of_range.404  if the JSON pointer can not be resolved
    */
    template<typename BasicJsonType>
    const BasicJsonType& get_unchecked(const BasicJsonType* ptr) const
    {
        for (const auto& reference_token : reference_tokens)
        {
            switch (ptr->type())
            {
                case detail::value_t::object:
                {
                    // use unchecked object access
                    ptr = &ptr->operator[](reference_token);
                    break;
                }

                case detail::value_t::array:
                {
                    if (JSON_HEDLEY_UNLIKELY(reference_token == "-"))
                    {
                        // "-" cannot be used for const access
                        JSON_THROW(detail::out_of_range::create(402, detail::concat("array index '-' (", std::to_string(ptr->m_data.m_value.array->size()), ") is out of range"), ptr));
                    }

                    // use unchecked array access
                    ptr = &ptr->operator[](array_index<BasicJsonType>(reference_token));
                    break;
                }

                case detail::value_t::null:
                case detail::value_t::string:
                case detail::value_t::boolean:
                case detail::value_t::number_integer:
                case detail::value_t::number_unsigned:
                case detail::value_t::number_float:
                case detail::value_t::binary:
                case detail::value_t::discarded:
                default:
                    JSON_THROW(detail::out_of_range::create(404, detail::concat("unresolved reference token '", reference_token, "'"), ptr));
            }
        }

        return *ptr;
    }

    /*!
    @throw parse_error.106   if an array index begins with '0'
    @throw parse_error.109   if an array index was not a number
    @throw out_of_range.402  if the array index '-' is used
    @throw out_of_range.404  if the JSON pointer can not be resolved
    */
    template<typename BasicJsonType>
    const BasicJsonType& get_checked(const BasicJsonType* ptr) const
    {
        for (const auto& reference_token : reference_tokens)
        {
            switch (ptr->type())
            {
                case detail::value_t::object:
                {
                    // note: at performs range check
                    ptr = &ptr->at(reference_token);
                    break;
                }

                case detail::value_t::array:
                {
                    if (JSON_HEDLEY_UNLIKELY(reference_token == "-"))
                    {
                        // "-" always fails the range check
                        JSON_THROW(detail::out_of_range::create(402, detail::concat(
                                "array index '-' (", std::to_string(ptr->m_data.m_value.array->size()),
                                ") is out of range"), ptr));
                    }

                    const auto idx = array_index<BasicJsonType>(reference_token);
                    // Bounds check before access to avoid exception with JSON_NOEXCEPTION
                    if (JSON_HEDLEY_UNLIKELY(idx >= ptr->m_data.m_value.array->size()))
                    {
                        JSON_THROW(detail::out_of_range::create(401, detail::concat(
                                "array index ", std::to_string(idx), " is out of range"), ptr));
                    }
                    ptr = &ptr->operator[](idx);
                    break;
                }

                case detail::value_t::null:
                case detail::value_t::string:
                case detail::value_t::boolean:
                case detail::value_t::number_integer:
                case detail::value_t::number_unsigned:
                case detail::value_t::number_float:
                case detail::value_t::binary:
                case detail::value_t::discarded:
                default:
                    JSON_THROW(detail::out_of_range::create(404, detail::concat("unresolved reference token '", reference_token, "'"), ptr));
            }
        }

        return *ptr;
    }

    /*!
    @brief return a pointer to the pointed to value, or `nullptr` if the
           pointer cannot be resolved because a key is missing, an array
           index is out of range, or the array index is "-"

    @note unlike get_checked(), this never throws for those cases, so it
          can be used to implement a non-throwing fallback (e.g. value())
          that also works when exceptions are disabled

    @throw parse_error.106   if an array index begins with '0'
    @throw parse_error.109   if an array index was not a number
    */
    template<typename BasicJsonType>
    const BasicJsonType* get_checked_or_null(const BasicJsonType* ptr) const
    {
        for (const auto& reference_token : reference_tokens)
        {
            switch (ptr->type())
            {
                case detail::value_t::object:
                {
                    const auto it = ptr->find(reference_token);
                    if (JSON_HEDLEY_UNLIKELY(it == ptr->end()))
                    {
                        return nullptr;
                    }
                    ptr = &*it;
                    break;
                }

                case detail::value_t::array:
                {
                    if (JSON_HEDLEY_UNLIKELY(reference_token == "-"))
                    {
                        // "-" always fails the range check
                        return nullptr;
                    }

                    // may throw parse_error.106/109 for a malformed index; an
                    // index that is syntactically valid but cannot be
                    // represented (out_of_range.404/410) is treated like an
                    // out-of-range index below
                    typename BasicJsonType::size_type idx{};
                    JSON_TRY
                    {
                        idx = array_index<BasicJsonType>(reference_token);
                    }
                    JSON_INTERNAL_CATCH (detail::out_of_range&)
                    {
                        return nullptr;
                    }

                    if (JSON_HEDLEY_UNLIKELY(idx >= ptr->m_data.m_value.array->size()))
                    {
                        return nullptr;
                    }
                    ptr = &ptr->operator[](idx);
                    break;
                }

                case detail::value_t::null:
                case detail::value_t::string:
                case detail::value_t::boolean:
                case detail::value_t::number_integer:
                case detail::value_t::number_unsigned:
                case detail::value_t::number_float:
                case detail::value_t::binary:
                case detail::value_t::discarded:
                default:
                    return nullptr;
            }
        }

        return ptr;
    }

    /*!
    @throw parse_error.106   if an array index begins with '0'
    @throw parse_error.109   if an array index was not a number
    */
    template<typename BasicJsonType>
    bool contains(const BasicJsonType* ptr) const
    {
        for (const auto& reference_token : reference_tokens)
        {
            switch (ptr->type())
            {
                case detail::value_t::object:
                {
                    if (!ptr->contains(reference_token))
                    {
                        // we did not find the key in the object
                        return false;
                    }

                    ptr = &ptr->operator[](reference_token);
                    break;
                }

                case detail::value_t::array:
                {
                    if (JSON_HEDLEY_UNLIKELY(reference_token == "-"))
                    {
                        // "-" always fails the range check
                        return false;
                    }
                    if (JSON_HEDLEY_UNLIKELY(reference_token.size() == 1 && !("0" <= reference_token && reference_token <= "9")))
                    {
                        // invalid char
                        return false;
                    }
                    if (JSON_HEDLEY_UNLIKELY(reference_token.size() > 1))
                    {
                        if (JSON_HEDLEY_UNLIKELY(!('1' <= reference_token[0] && reference_token[0] <= '9')))
                        {
                            // the first char should be between '1' and '9'
                            return false;
                        }
                        for (std::size_t i = 1; i < reference_token.size(); i++)
                        {
                            if (JSON_HEDLEY_UNLIKELY(!('0' <= reference_token[i] && reference_token[i] <= '9')))
                            {
                                // other char should be between '0' and '9'
                                return false;
                            }
                        }
                    }

                    // the reference token consists only of digits at this point (cf. checks
                    // above); however, its numeric value might not be representable, in which
                    // case array_index() would throw out_of_range.404/410 -- contains() must
                    // not throw (see #5395), so such a reference token is treated as "not found"
                    errno = 0; // strtoull() does not reset errno on success
                    char* p_end = nullptr; // NOLINT(misc-const-correctness)
                    const unsigned long long magnitude = std::strtoull(reference_token.c_str(), &p_end, 10); // NOLINT(runtime/int)
                    if (JSON_HEDLEY_UNLIKELY(errno == ERANGE // the value exceeds ULLONG_MAX
                                             || magnitude >= static_cast<unsigned long long>((std::numeric_limits<typename BasicJsonType::size_type>::max)()))) // NOLINT(runtime/int)
                    {
                        // the array index cannot be represented as size_type
                        return false;
                    }

                    const auto idx = array_index<BasicJsonType>(reference_token);
                    if (idx >= ptr->size())
                    {
                        // index out of range
                        return false;
                    }

                    ptr = &ptr->operator[](idx);
                    break;
                }

                case detail::value_t::null:
                case detail::value_t::string:
                case detail::value_t::boolean:
                case detail::value_t::number_integer:
                case detail::value_t::number_unsigned:
                case detail::value_t::number_float:
                case detail::value_t::binary:
                case detail::value_t::discarded:
                default:
                {
                    // we do not expect primitive values if there is still a
                    // reference token to process
                    return false;
                }
            }
        }

        // no reference token left means we found a primitive value
        return true;
    }

    /*!
    @brief split the string input to reference tokens

    @note This function is only called by the json_pointer constructor.
          All exceptions below are documented there.

    @throw parse_error.107  if the pointer is not empty or begins with '/'
    @throw parse_error.108  if character '~' is not followed by '0' or '1'
    */
    static std::vector<string_t> split(const string_t& reference_string)
    {
        std::vector<string_t> result;

        // special case: empty reference string -> no reference tokens
        if (reference_string.empty())
        {
            return result;
        }

        // check if a nonempty reference string begins with slash
        if (JSON_HEDLEY_UNLIKELY(reference_string[0] != '/'))
        {
            JSON_THROW(detail::parse_error::create(107, 1, detail::concat("JSON pointer must be empty or begin with '/' - was: '", reference_string, "'"), nullptr));
        }

        // extract the reference tokens:
        // - slash: position of the last read slash (or end of string)
        // - start: position after the previous slash
        for (
            // search for the first slash after the first character
            std::size_t slash = reference_string.find_first_of('/', 1),
            // set the beginning of the first reference token
            start = 1;
            // we can stop if start == 0 (if slash == string_t::npos)
            start != 0;
            // set the beginning of the next reference token
            // (will eventually be 0 if slash == string_t::npos)
            start = (slash == string_t::npos) ? 0 : slash + 1,
            // find next slash
            slash = reference_string.find_first_of('/', start))
        {
            // use the text between the beginning of the reference token
            // (start) and the last slash (slash).
            const auto count = (slash == string_t::npos ? reference_string.size() : slash) - start;
            auto reference_token = string_t(reference_string.data() + start, count);

            // check reference tokens are properly escaped
            for (std::size_t pos = reference_token.find_first_of('~');
                    pos != string_t::npos;
                    pos = reference_token.find_first_of('~', pos + 1))
            {
                JSON_ASSERT(reference_token[pos] == '~');

                // ~ must be followed by 0 or 1
                if (JSON_HEDLEY_UNLIKELY(pos == reference_token.size() - 1 ||
                                         (reference_token[pos + 1] != '0' &&
                                          reference_token[pos + 1] != '1')))
                {
                    JSON_THROW(detail::parse_error::create(108, 0, "escape character '~' must be followed with '0' or '1'", nullptr));
                }
            }

            // finally, store the reference token
            detail::unescape(reference_token);
            result.push_back(reference_token);
        }

        return result;
    }

  private:
    /*!
    @param[in] reference_string  the reference string to the current value
    @param[in] value             the value to consider
    @param[in,out] result        the result object to insert values to

    @note Empty objects or arrays are flattened to `null`.
    */
    template<typename BasicJsonType>
    static void flatten(const string_t& reference_string,
                        const BasicJsonType& value,
                        BasicJsonType& result)
    {
        switch (value.type())
        {
            case detail::value_t::array:
            {
                if (value.m_data.m_value.array->empty())
                {
                    // flatten empty array as null
                    result[reference_string] = nullptr;
                }
                else
                {
                    // iterate array and use index as a reference string
                    for (std::size_t i = 0; i < value.m_data.m_value.array->size(); ++i)
                    {
                        flatten(detail::concat<string_t>(reference_string, '/', std::to_string(i)),
                                value.m_data.m_value.array->operator[](i), result);
                    }
                }
                break;
            }

            case detail::value_t::object:
            {
                if (value.m_data.m_value.object->empty())
                {
                    // flatten empty object as null
                    result[reference_string] = nullptr;
                }
                else
                {
                    // iterate object and use keys as reference string
                    for (const auto& element : *value.m_data.m_value.object)
                    {
                        flatten(detail::concat<string_t>(reference_string, '/', detail::escape(element.first)), element.second, result);
                    }
                }
                break;
            }

            case detail::value_t::null:
            case detail::value_t::string:
            case detail::value_t::boolean:
            case detail::value_t::number_integer:
            case detail::value_t::number_unsigned:
            case detail::value_t::number_float:
            case detail::value_t::binary:
            case detail::value_t::discarded:
            default:
            {
                // add a primitive value with its reference string
                result[reference_string] = value;
                break;
            }
        }
    }

    /*!
    @param[in] value  flattened JSON

    @return unflattened JSON

    @throw parse_error.109 if array index is not a number
    @throw type_error.314  if value is not an object
    @throw type_error.315  if object values are not primitive
    @throw type_error.313  if value cannot be unflattened
    */
    template<typename BasicJsonType>
    static BasicJsonType
    unflatten(const BasicJsonType& value)
    {
        if (JSON_HEDLEY_UNLIKELY(!value.is_object()))
        {
            JSON_THROW(detail::type_error::create(314, "only objects can be unflattened", &value));
        }

        BasicJsonType result;

        // collect the pointer prefixes that have a reference token 0 among
        // their children; the values below them are arrays, all others are
        // objects (see array_parents_t)
        array_parents_t array_parents;
        for (const auto& element : *value.m_data.m_value.object)
        {
            json_pointer ptr(element.first);
            std::vector<string_t> prefix;
            for (auto& reference_token : ptr.reference_tokens)
            {
                if (reference_token == "0")
                {
                    array_parents.insert(prefix);
                }
                prefix.push_back(std::move(reference_token));
            }
        }

        // iterate the JSON object values
        for (const auto& element : *value.m_data.m_value.object)
        {
            if (JSON_HEDLEY_UNLIKELY(!element.second.is_primitive()))
            {
                JSON_THROW(detail::type_error::create(315, "values in object must be primitive", &element.second));
            }

            // Assign the value to the reference pointed to by JSON pointer. Note
            // that if the JSON pointer is "" (i.e., points to the whole value),
            // function get_and_create returns a reference to the result itself.
            // An assignment will then create a primitive value.
            json_pointer(element.first).get_and_create(result, array_parents) = element.second;
        }

        return result;
    }

    // can't use the conversion operator because of ambiguity
    json_pointer<string_t> convert() const&
    {
        json_pointer<string_t> result;
        result.reference_tokens = reference_tokens;
        return result;
    }

    json_pointer<string_t> convert()&&
    {
        json_pointer<string_t> result;
        result.reference_tokens = std::move(reference_tokens);
        return result;
    }

  public:
#if JSON_HAS_THREE_WAY_COMPARISON
    /// @brief compares two JSON pointers for equality
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_eq/
    template<typename RefStringTypeRhs>
    bool operator==(const json_pointer<RefStringTypeRhs>& rhs) const noexcept
    {
        return reference_tokens == rhs.reference_tokens;
    }

    /// @brief compares JSON pointer and string for equality
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_eq/
    JSON_HEDLEY_DEPRECATED_FOR(3.11.2, operator==(json_pointer))
    bool operator==(const string_t& rhs) const
    {
        return *this == json_pointer(rhs);
    }

    /// @brief 3-way compares two JSON pointers
    template<typename RefStringTypeRhs>
    std::strong_ordering operator<=>(const json_pointer<RefStringTypeRhs>& rhs) const noexcept // *NOPAD*
    {
        return  reference_tokens <=> rhs.reference_tokens; // *NOPAD*
    }
#else
    /// @brief compares two JSON pointers for equality
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_eq/
    template<typename RefStringTypeLhs, typename RefStringTypeRhs>
    // NOLINTNEXTLINE(readability-redundant-declaration)
    friend bool operator==(const json_pointer<RefStringTypeLhs>& lhs,
                           const json_pointer<RefStringTypeRhs>& rhs) noexcept;

    /// @brief compares JSON pointer and string for equality
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_eq/
    template<typename RefStringTypeLhs, typename StringType>
    // NOLINTNEXTLINE(readability-redundant-declaration)
    friend bool operator==(const json_pointer<RefStringTypeLhs>& lhs,
                           const StringType& rhs);

    /// @brief compares string and JSON pointer for equality
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_eq/
    template<typename RefStringTypeRhs, typename StringType>
    // NOLINTNEXTLINE(readability-redundant-declaration)
    friend bool operator==(const StringType& lhs,
                           const json_pointer<RefStringTypeRhs>& rhs);

    /// @brief compares two JSON pointers for inequality
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_ne/
    template<typename RefStringTypeLhs, typename RefStringTypeRhs>
    // NOLINTNEXTLINE(readability-redundant-declaration)
    friend bool operator!=(const json_pointer<RefStringTypeLhs>& lhs,
                           const json_pointer<RefStringTypeRhs>& rhs) noexcept;

    /// @brief compares JSON pointer and string for inequality
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_ne/
    template<typename RefStringTypeLhs, typename StringType>
    // NOLINTNEXTLINE(readability-redundant-declaration)
    friend bool operator!=(const json_pointer<RefStringTypeLhs>& lhs,
                           const StringType& rhs);

    /// @brief compares string and JSON pointer for inequality
    /// @sa https://json.nlohmann.me/api/json_pointer/operator_ne/
    template<typename RefStringTypeRhs, typename StringType>
    // NOLINTNEXTLINE(readability-redundant-declaration)
    friend bool operator!=(const StringType& lhs,
                           const json_pointer<RefStringTypeRhs>& rhs);

    /// @brief compares two JSON pointer for less-than
    template<typename RefStringTypeLhs, typename RefStringTypeRhs>
    // NOLINTNEXTLINE(readability-redundant-declaration)
    friend bool operator<(const json_pointer<RefStringTypeLhs>& lhs,
                          const json_pointer<RefStringTypeRhs>& rhs) noexcept;
#endif

  private:
    /// the reference tokens
    std::vector<string_t> reference_tokens;
};

#if !JSON_HAS_THREE_WAY_COMPARISON
// functions cannot be defined inside the class due to ODR violations
template<typename RefStringTypeLhs, typename RefStringTypeRhs>
inline bool operator==(const json_pointer<RefStringTypeLhs>& lhs,
                       const json_pointer<RefStringTypeRhs>& rhs) noexcept
{
    return lhs.reference_tokens == rhs.reference_tokens;
}

template<typename RefStringTypeLhs,
         typename StringType = typename json_pointer<RefStringTypeLhs>::string_t>
JSON_HEDLEY_DEPRECATED_FOR(3.11.2, operator==(json_pointer, json_pointer))
inline bool operator==(const json_pointer<RefStringTypeLhs>& lhs,
                       const StringType& rhs)
{
    return lhs == json_pointer<RefStringTypeLhs>(rhs);
}

template<typename RefStringTypeRhs,
         typename StringType = typename json_pointer<RefStringTypeRhs>::string_t>
JSON_HEDLEY_DEPRECATED_FOR(3.11.2, operator==(json_pointer, json_pointer))
inline bool operator==(const StringType& lhs,
                       const json_pointer<RefStringTypeRhs>& rhs)
{
    return json_pointer<RefStringTypeRhs>(lhs) == rhs;
}

template<typename RefStringTypeLhs, typename RefStringTypeRhs>
inline bool operator!=(const json_pointer<RefStringTypeLhs>& lhs,
                       const json_pointer<RefStringTypeRhs>& rhs) noexcept
{
    return !(lhs == rhs);
}

template<typename RefStringTypeLhs,
         typename StringType = typename json_pointer<RefStringTypeLhs>::string_t>
JSON_HEDLEY_DEPRECATED_FOR(3.11.2, operator!=(json_pointer, json_pointer))
inline bool operator!=(const json_pointer<RefStringTypeLhs>& lhs,
                       const StringType& rhs)
{
    return !(lhs == rhs);
}

template<typename RefStringTypeRhs,
         typename StringType = typename json_pointer<RefStringTypeRhs>::string_t>
JSON_HEDLEY_DEPRECATED_FOR(3.11.2, operator!=(json_pointer, json_pointer))
inline bool operator!=(const StringType& lhs,
                       const json_pointer<RefStringTypeRhs>& rhs)
{
    return !(lhs == rhs);
}

template<typename RefStringTypeLhs, typename RefStringTypeRhs>
inline bool operator<(const json_pointer<RefStringTypeLhs>& lhs,
                      const json_pointer<RefStringTypeRhs>& rhs) noexcept
{
    return lhs.reference_tokens < rhs.reference_tokens;
}
#endif

/*!
@brief an explicit batch resolution plan for JSON pointers

A json_pointer_plan collects several JSON pointer requests, merges their
common reference-token prefixes into a single traversal tree (the "compile"
step, see add()), and resolves all requests against a `const` JSON value in
one pass (see execute()), so every shared prefix is accessed exactly once.

The JSON value is never modified (unlike `operator[]`, no keys are inserted
for missing intermediate objects), and each request yields an independently
owned copy of the addressed value plus a classified error. All requests that
fail at the same shared prefix observe the very same error source. Numeric
reference tokens are only interpreted as array indices when the current
value is an array; for objects they are ordinary keys.

@note This class does not change the behavior of basic_json::at; it is a
      separate, non-throwing batch API.
*/
template<typename BasicJsonType>
class json_pointer_plan
{
  public:
    /// @brief string type used by the JSON value
    using string_t = typename BasicJsonType::string_t;
    /// @brief JSON pointer type of the plan
    using pointer_type = json_pointer<string_t>;
    /// @brief size type of the JSON value
    using size_type = typename BasicJsonType::size_type;

    /// @brief classification of the outcome of a single request
    enum class status
    {
        value,               ///< the request resolves to an existing non-null value
        null_value,          ///< the request resolves to an existing null value
        missing_key,         ///< an object key on the path does not exist
        wrong_container,     ///< a reference token was applied to a value that is not a matching container
        array_out_of_bounds  ///< an array index on the path is out of bounds
    };

    /// @brief classified error shared by all requests affected by the same failing prefix
    struct error
    {
        status kind;      ///< the failure classification
        string_t prefix;  ///< the JSON pointer up to and including the failing reference token
        string_t token;   ///< the reference token that could not be resolved
        string_t message; ///< a human-readable description of the failure
    };

    /// @brief the independently owned outcome of a single request
    struct result
    {
        status outcome = status::missing_key;                ///< classification of this outcome
        string_t pointer{};                                  ///< the requested JSON pointer
        std::size_t canonical = 0;                           ///< slot of the first identical request (identity of duplicates)
        BasicJsonType value = nullptr;                       ///< independently owned copy of the addressed value
        std::shared_ptr<const error> error_source = nullptr; ///< shared error source; `nullptr` on success

        /// @brief whether the request resolved to an existing value (including `null`)
        bool ok() const noexcept
        {
            return outcome == status::value || outcome == status::null_value;
        }
    };

    /// @brief access statistics of a single execute() pass
    struct statistics
    {
        std::size_t nodes_visited = 0;                 ///< number of trie nodes resolved against the JSON value
        std::map<string_t, std::size_t> prefix_visits; ///< number of accesses per JSON pointer prefix
    };

    /// @brief add a request for the JSON pointer @a ptr; returns the request slot
    /// @note Adding the same pointer twice preserves the identity of each
    ///       query: every call gets its own slot, but duplicate queries share
    ///       one trie leaf and are resolved in a single traversal.
    std::size_t add(const pointer_type& ptr)
    {
        const string_t key = ptr.to_string();

        const auto duplicate = dedupe_.find(key);
        if (duplicate != dedupe_.end())
        {
            // duplicate query: keep its own request slot (identity preserved)
            // but resolve it through the already compiled trie leaf
            const std::size_t first = duplicate->second;
            const std::size_t slot = requests_.size();
            requests_.push_back(ptr);
            canonical_.push_back(first);
            leaves_.push_back(leaves_[first]);
            nodes_[leaves_[first]].requests.push_back(slot);
            return slot;
        }

        // compile the pointer into the shared prefix tree
        std::size_t current = 0; // node 0 is the root (empty pointer)
        string_t prefix;
        for (const auto& token : pointer_type::split(key))
        {
            prefix = detail::concat<string_t>(prefix, '/', detail::escape(token));
            auto& children = nodes_[current].children;
            const auto it = children.find(token);
            if (it == children.end())
            {
                const std::size_t next = nodes_.size();
                children.emplace(token, next);
                node child;
                child.prefix = prefix;
                nodes_.push_back(std::move(child));
                current = next;
            }
            else
            {
                current = it->second;
            }
        }

        const std::size_t slot = requests_.size();
        requests_.push_back(ptr);
        canonical_.push_back(slot);
        leaves_.push_back(current);
        dedupe_.emplace(key, slot);
        nodes_[current].requests.push_back(slot);
        return slot;
    }

    /// @brief add a request for the JSON pointer string @a s; returns the request slot
    std::size_t add(const string_t& s)
    {
        return add(pointer_type(s));
    }

    /// @brief the number of added requests (duplicates keep their own slots)
    std::size_t request_count() const noexcept
    {
        return requests_.size();
    }

    /// @brief the number of distinct JSON pointers in the plan
    std::size_t unique_count() const noexcept
    {
        return dedupe_.size();
    }

    /// @brief the JSON pointer requested by slot @a slot
    const pointer_type& request(std::size_t slot) const
    {
        return requests_.at(slot);
    }

    /// @brief the merged prefixes of the compiled plan in construction order
    std::vector<string_t> prefixes() const
    {
        std::vector<string_t> result;
        result.reserve(nodes_.size());
        for (const auto& n : nodes_)
        {
            result.push_back(n.prefix);
        }
        return result;
    }

    /*!
    @brief resolve all requests against @a root in a single pass

    Every shared prefix of the requested pointers is accessed exactly once.
    The value @a root is only read, never modified. Each returned result
    holds an independently owned deep copy of the addressed value, so the
    results stay valid and unchanged when @a root is modified afterwards.

    @param[in] root  the JSON value to resolve the requests against
    @param[in,out] stats  optional access statistics of this pass
    @return one result per request, in the order the requests were added
    */
    std::vector<result> execute(const BasicJsonType& root, statistics* stats = nullptr) const
    {
        std::vector<result> results(requests_.size());
        for (std::size_t slot = 0; slot < requests_.size(); ++slot)
        {
            results[slot].pointer = requests_[slot].to_string();
            results[slot].canonical = canonical_[slot];
        }

        // iterative depth-first traversal of the compiled prefix tree
        std::vector<std::pair<std::size_t, const BasicJsonType*>> stack;
        stack.emplace_back(0, &root);
        while (!stack.empty())
        {
            const auto frame = stack.back();
            stack.pop_back();
            const std::size_t node_index = frame.first;
            const BasicJsonType* value = frame.second;
            const node& current = nodes_[node_index];

            if (stats != nullptr)
            {
                ++stats->nodes_visited;
                ++stats->prefix_visits[current.prefix];
            }

            // resolve all requests ending at this node
            for (const auto slot : current.requests)
            {
                auto& res = results[slot];
                res.value = *value; // independently owned deep copy
                res.outcome = value->is_null() ? status::null_value : status::value;
            }

            // resolve each child token once; a failure is shared by the
            // requests of the whole subtree below the failing prefix
            for (const auto& child : current.children)
            {
                const string_t& token = child.first;
                const node& child_node = nodes_[child.second];
                const BasicJsonType* next = nullptr;
                std::shared_ptr<const error> failure;

                switch (value->type())
                {
                    case detail::value_t::object:
                    {
                        // numeric tokens are ordinary object keys here and
                        // are never reinterpreted as array indices
                        const auto it = value->find(token);
                        if (JSON_HEDLEY_UNLIKELY(it == value->end()))
                        {
                            failure = make_error(status::missing_key, child_node, token,
                                                 detail::concat<string_t>("unresolved reference token '", token, "'"));
                        }
                        else
                        {
                            next = std::addressof(*it);
                        }
                        break;
                    }

                    case detail::value_t::array:
                    {
                        size_type idx = 0;
                        if (!parse_array_index(token, idx))
                        {
                            failure = make_error(status::array_out_of_bounds, child_node, token,
                                                 detail::concat<string_t>("array index '", token, "' is not a valid array index"));
                        }
                        else if (JSON_HEDLEY_UNLIKELY(idx >= value->size()))
                        {
                            failure = make_error(status::array_out_of_bounds, child_node, token,
                                                 detail::concat<string_t>("array index '", token, "' is out of range"));
                        }
                        else
                        {
                            next = std::addressof(value->operator[](idx));
                        }
                        break;
                    }

                    case detail::value_t::null:
                    case detail::value_t::string:
                    case detail::value_t::boolean:
                    case detail::value_t::number_integer:
                    case detail::value_t::number_unsigned:
                    case detail::value_t::number_float:
                    case detail::value_t::binary:
                    case detail::value_t::discarded:
                    default:
                    {
                        failure = make_error(status::wrong_container, child_node, token,
                                             detail::concat<string_t>("cannot resolve reference token '", token, "' on a non-container value"));
                        break;
                    }
                }

                if (failure != nullptr)
                {
                    fail_subtree(child.second, failure, results);
                }
                else
                {
                    stack.emplace_back(child.second, next);
                }
            }
        }

        return results;
    }

  private:
    /// a node of the compiled prefix tree
    struct node
    {
        string_t prefix;                        ///< JSON pointer from the root to this node
        std::map<string_t, std::size_t> children; ///< reference token -> child node index
        std::vector<std::size_t> requests;      ///< request slots resolved at this node
    };

    /// create the shared error source for a failing prefix
    static std::shared_ptr<const error> make_error(status kind, const node& where, const string_t& token, const string_t& message)
    {
        error e;
        e.kind = kind;
        e.prefix = where.prefix;
        e.token = token;
        e.message = message;
        return std::make_shared<const error>(std::move(e));
    }

    /// assign the shared error source @a failure to every request below node @a node_index
    void fail_subtree(std::size_t node_index, const std::shared_ptr<const error>& failure, std::vector<result>& results) const
    {
        std::vector<std::size_t> stack(1, node_index);
        while (!stack.empty())
        {
            const std::size_t current = stack.back();
            stack.pop_back();
            const node& n = nodes_[current];
            for (const auto slot : n.requests)
            {
                auto& res = results[slot];
                res.outcome = failure->kind;
                res.error_source = failure; // same source identity for all affected requests
            }
            for (const auto& child : n.children)
            {
                stack.push_back(child.second);
            }
        }
    }

    /// parse @a s as an array index; returns false for '-', malformed, or unrepresentable indices
    static bool parse_array_index(const string_t& s, size_type& idx)
    {
        // '-' (the element past the end) and empty tokens never address an element
        if (s.empty() || (s.size() == 1 && s[0] == '-'))
        {
            return false;
        }

        // leading zeros are not allowed (cf. parse_error.106)
        if (s.size() > 1 && s[0] == '0')
        {
            return false;
        }

        // only digits are allowed (cf. parse_error.109)
        for (const auto c : s)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
        }

        errno = 0; // strtoull() does not reset errno on success
        char* p_end = nullptr; // NOLINT(misc-const-correctness)
        const unsigned long long magnitude = std::strtoull(s.c_str(), &p_end, 10); // NOLINT(runtime/int)
        if (JSON_HEDLEY_UNLIKELY(errno == ERANGE // the value exceeds ULLONG_MAX
                                 || magnitude >= static_cast<unsigned long long>((std::numeric_limits<size_type>::max)()))) // NOLINT(runtime/int)
        {
            // the array index cannot be represented as size_type
            return false;
        }

        idx = static_cast<size_type>(magnitude);
        return true;
    }

    std::vector<node> nodes_{1};            ///< prefix tree; node 0 is the root (empty pointer)
    std::vector<pointer_type> requests_;    ///< one slot per added request
    std::vector<std::size_t> canonical_;    ///< request slot -> first slot with the same pointer
    std::vector<std::size_t> leaves_;       ///< request slot -> prefix tree leaf
    std::map<string_t, std::size_t> dedupe_; ///< pointer string -> first request slot
};

NLOHMANN_JSON_NAMESPACE_END
