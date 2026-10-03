//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cmath> // isfinite, isnan, ldexp, trunc
#include <limits> // numeric_limits

#include <nlohmann/json.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

/// @brief result of a read-only structural comparison of two JSON values
///
/// `equal` is true iff both values are structurally equal under the exact
/// numeric comparison rules documented for structural_compare().
/// If `equal` is false, `first_difference` holds a JSON Pointer (RFC 6901,
/// properly escaped) to the first value that differs; an empty pointer ("")
/// denotes the document root.
template<typename BasicJsonType>
struct structural_compare_result
{
    bool equal = false;
    typename BasicJsonType::json_pointer first_difference {};
};

namespace detail
{

// exact equality of two integer values (signed and/or unsigned), without
// ever converting through a floating-point type
template<typename BasicJsonType>
inline bool structural_integer_equal(const BasicJsonType& a, const BasicJsonType& b)
{
    using number_integer_t  = typename BasicJsonType::number_integer_t;
    using number_unsigned_t = typename BasicJsonType::number_unsigned_t;

    // note: is_number_integer() also covers unsigned values, so the exact
    // value_t must be inspected to tell signed and unsigned integers apart
    const bool a_signed = a.type() == value_t::number_integer;
    const bool b_signed = b.type() == value_t::number_integer;

    if (a_signed && b_signed)
    {
        return a.template get<number_integer_t>() == b.template get<number_integer_t>();
    }
    if (!a_signed && !b_signed)
    {
        return a.template get<number_unsigned_t>() == b.template get<number_unsigned_t>();
    }
    // mixed signed/unsigned: a negative signed value can never equal an
    // unsigned value; otherwise compare in the unsigned domain (no overflow)
    const number_integer_t s = a_signed ? a.template get<number_integer_t>()
                                        : b.template get<number_integer_t>();
    const number_unsigned_t u = a_signed ? b.template get<number_unsigned_t>()
                                         : a.template get<number_unsigned_t>();
    return s >= 0 && static_cast<number_unsigned_t>(s) == u;
}

// exact equality of an integer (signed or unsigned) and a floating-point value
template<typename BasicJsonType>
inline bool structural_integer_float_equal(const BasicJsonType& i, const BasicJsonType& f)
{
    using number_integer_t  = typename BasicJsonType::number_integer_t;
    using number_unsigned_t = typename BasicJsonType::number_unsigned_t;
    using number_float_t    = typename BasicJsonType::number_float_t;

    const number_float_t d = f.template get<number_float_t>();

    // NaN policy: NaN is rejected - it is never equal to any value.
    // Infinity: no integer is equal to +/-infinity.
    if (!std::isfinite(d))
    {
        return false;
    }
    // a fractional value never equals an integer
    if (std::trunc(d) != d)
    {
        return false;
    }
    // 2^digits is exactly representable in every IEEE floating-point type, so
    // these bounds are exact; an integral d strictly inside them is guaranteed
    // to fit the target integer type (no overflow, no wraparound, no UB).
    if (i.type() == value_t::number_integer)
    {
        const number_float_t bound = std::ldexp(static_cast<number_float_t>(1),
                                                std::numeric_limits<number_integer_t>::digits);
        if (d < -bound || d >= bound)
        {
            return false;
        }
        return static_cast<number_integer_t>(d) == i.template get<number_integer_t>();
    }
    const number_float_t bound = std::ldexp(static_cast<number_float_t>(1),
                                            std::numeric_limits<number_unsigned_t>::digits);
    if (d < 0 || d >= bound)
    {
        return false;
    }
    return static_cast<number_unsigned_t>(d) == i.template get<number_unsigned_t>();
}

// exact equality of two floating-point values
template<typename BasicJsonType>
inline bool structural_float_equal(const BasicJsonType& a, const BasicJsonType& b)
{
    using number_float_t = typename BasicJsonType::number_float_t;

    const number_float_t da = a.template get<number_float_t>();
    const number_float_t db = b.template get<number_float_t>();

    // NaN policy: NaN is rejected - it is never equal to any value,
    // not even to itself.
    if (std::isnan(da) || std::isnan(db))
    {
        return false;
    }
    // IEEE 754 semantics for the rest: +inf == +inf, -inf == -inf,
    // inf != any finite value, and -0.0 == 0.0.
    return da == db;
}

// exact numeric equality across all three JSON number types
template<typename BasicJsonType>
inline bool structural_number_equal(const BasicJsonType& a, const BasicJsonType& b)
{
    const bool a_float = a.is_number_float();
    const bool b_float = b.is_number_float();

    if (!a_float && !b_float)
    {
        return structural_integer_equal(a, b);
    }
    if (a_float && b_float)
    {
        return structural_float_equal(a, b);
    }
    return a_float ? structural_integer_float_equal(b, a)
                   : structural_integer_float_equal(a, b);
}

template<typename BasicJsonType>
bool structural_compare_impl(const BasicJsonType& a, const BasicJsonType& b,
                             typename BasicJsonType::json_pointer& path)
{
    // numbers form a single comparison class: signed integers, unsigned
    // integers and floating-point values are compared by exact numeric value
    if (a.is_number() && b.is_number())
    {
        return structural_number_equal(a, b);
    }

    // all other types must match exactly; in particular strings are never
    // coerced to numbers ("1" != 1) and booleans are never coerced either
    if (a.type() != b.type())
    {
        return false;
    }

    switch (a.type())
    {
        case value_t::object:
        {
            // key order is insignificant: objects are maps
            if (a.size() != b.size())
            {
                return false;
            }
            for (auto it = a.begin(); it != a.end(); ++it)
            {
                auto bt = b.find(it.key());
                if (bt == b.end())
                {
                    path /= it.key();
                    return false;
                }
                path /= it.key();
                const bool eq = structural_compare_impl(it.value(), *bt, path);
                if (!eq)
                {
                    return false;
                }
                path.pop_back();
            }
            return true;
        }

        case value_t::array:
        {
            // element order is significant: arrays are sequences
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t idx = 0; idx < a.size(); ++idx)
            {
                path /= idx;
                const bool eq = structural_compare_impl(a[idx], b[idx], path);
                if (!eq)
                {
                    return false;
                }
                path.pop_back();
            }
            return true;
        }

        case value_t::string:
        case value_t::boolean:
        case value_t::binary:
        case value_t::null:
        case value_t::number_integer:
        case value_t::number_unsigned:
        case value_t::number_float:
        case value_t::discarded:
        default:
            // same-type leaf values: the library's own operator== is exact
            // here (no cross-type coercion can occur at this point)
            return a == b;
    }
}

} // namespace detail

/// @brief read-only structural comparison of two JSON values
///
/// Compares `a` and `b` without modifying either operand and reports whether
/// they are structurally equal, together with a JSON Pointer to the first
/// difference. Comparison rules:
///
/// - signed integers, unsigned integers and floating-point numbers are
///   compared by exact numeric value; integers are never converted through
///   `double`, so distinct integers beyond 2^53 (e.g. 9007199254740992 and
///   9007199254740993) always compare unequal;
/// - an integer equals a floating-point number only if the floating-point
///   value is finite, integral and exactly representable in the domain of
///   the integer (1 == 1.0, but 9007199254740993 != 9007199254740992.0);
/// - NaN is rejected: it is never equal to any value, not even NaN;
/// - -0.0 equals 0.0 (and the integer 0); +inf equals +inf and -inf equals
///   -inf, but infinity never equals a finite value;
/// - object key order is insignificant; array element order is significant;
/// - strings are never coerced to numbers ("1" != 1) and vice versa;
/// - this function is independent of basic_json::operator== whose semantics
///   remain unchanged.
template<typename BasicJsonType>
structural_compare_result<BasicJsonType> structural_compare(const BasicJsonType& a, const BasicJsonType& b)
{
    structural_compare_result<BasicJsonType> result;
    typename BasicJsonType::json_pointer path;
    result.equal = detail::structural_compare_impl(a, b, path);
    if (!result.equal)
    {
        result.first_difference = path;
    }
    return result;
}

/// @brief convenience wrapper returning only the equality flag
/// @sa structural_compare
template<typename BasicJsonType>
bool structural_equal(const BasicJsonType& a, const BasicJsonType& b)
{
    return structural_compare(a, b).equal;
}

NLOHMANN_JSON_NAMESPACE_END
