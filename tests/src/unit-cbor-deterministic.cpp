//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++ (supporting code)
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#include "doctest_compatibility.h"

#include <nlohmann/json.hpp>
using nlohmann::json;
using nlohmann::ordered_json;

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{

std::string to_hex(const std::vector<std::uint8_t>& v)
{
    std::ostringstream oss;
    for (const auto byte : v)
    {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<std::uint8_t> from_hex(const std::string& hex)
{
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        result.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return result;
}

void check_vector(const json& j, const std::string& expected_hex)
{
    const auto result = json::to_cbor_deterministic(j);
    // print the hexadecimal encoding for manual inspection
    std::cout << to_hex(result) << std::endl;
    CHECK(to_hex(result) == expected_hex);
    CHECK(result == from_hex(expected_hex));
}

} // namespace

TEST_CASE("deterministic CBOR serialization")
{
    SECTION("object key order is independent of insertion order")
    {
        // keys "b" and "aa": length-first ordering puts "b" (encoded
        // length 2) before "aa" (encoded length 3), whereas plain
        // lexicographic string ordering would put "aa" first
        const ordered_json j1 = {{"b", 1}, {"aa", 2}};
        const ordered_json j2 = {{"aa", 2}, {"b", 1}};

        const auto v1 = json::to_cbor_deterministic(j1);
        const auto v2 = json::to_cbor_deterministic(j2);

        std::cout << to_hex(v1) << std::endl;
        std::cout << to_hex(v2) << std::endl;

        CHECK(v1 == v2);
        CHECK(to_hex(v1) == "a2616201626161" "02");

        // plain to_cbor follows the (lexicographic) container order and
        // is unchanged by the new entry point
        CHECK(to_hex(json::to_cbor(j1)) == "a262616102616201");
        CHECK(to_hex(json::to_cbor(j2)) == "a262616102616201");
        CHECK(json::to_cbor(j1) != json::to_cbor_deterministic(j1));
    }

    SECTION("key ordering uses encoded length, then bytes")
    {
        // a 24-character key needs a two-byte length head (0x78 0x18),
        // so the one-character key "y" (encoded length 2) sorts first
        const json j = {{std::string(24, 'x'), 1}, {"y", 2}};
        const auto v = json::to_cbor_deterministic(j);
        std::cout << to_hex(v) << std::endl;

        std::vector<std::uint8_t> expected = {0xa2, 0x61, 0x79, 0x02, 0x78, 0x18};
        expected.insert(expected.end(), 24, 'x');
        expected.push_back(0x01);
        CHECK(v == expected);
    }

    SECTION("nested objects are sorted recursively")
    {
        const ordered_json j =
        {
            {"z", ordered_json::array({ordered_json{{"b", 1}, {"aa", 2}}})},
            {"a", ordered_json{{"b", 1}, {"aa", 2}}}
        };
        check_vector(j, "a26161a2616201626161" "02617a81a2616201626161" "02");
    }

    SECTION("array order is preserved")
    {
        check_vector(json::array({3, 1, 2}), "83030102");
    }

    SECTION("integer boundaries use the shortest encoding")
    {
        check_vector(json(0), "00");
        check_vector(json(23), "17");
        check_vector(json(24), "1818");
        check_vector(json(255), "18ff");
        check_vector(json(256), "190100");
        check_vector(json(65535), "19ffff");
        check_vector(json(65536), "1a00010000");
        check_vector(json(4294967295u), "1affffffff");
        check_vector(json(4294967296ull), "1b0000000100000000");
        check_vector(json((std::numeric_limits<std::uint64_t>::max)()), "1bffffffffffffffff");
        check_vector(json(-1), "20");
        check_vector(json(-24), "37");
        check_vector(json(-25), "3818");
        check_vector(json(-256), "38ff");
        check_vector(json(-257), "390100");
        check_vector(json(-65536), "39ffff");
        check_vector(json(-65537), "3a00010000");
        check_vector(json(-4294967296ll), "3affffffff");
        check_vector(json(-4294967297ll), "3b0000000100000000");
        check_vector(json((std::numeric_limits<std::int64_t>::min)()), "3b7fffffffffffffff");
    }

    SECTION("floats use the shortest lossless encoding")
    {
        // exactly representable in half precision
        check_vector(json(1.5), "f93e00");
        check_vector(json(0.0), "f90000");
        check_vector(json(65504.0), "f97bff"); // largest half
        // not representable in half, but in single precision
        check_vector(json(100000.0), "fa47c35000");
        check_vector(json(1.00048828125), "fa3f801000"); // 1 + 2^-11
        // not representable in single precision: no truncation
        check_vector(json(0.1), "fb3fb999999999999a");
        check_vector(json(1.0000000000000002), "fb3ff0000000000001");
    }

    SECTION("-0.0 keeps its sign")
    {
        const json j = -0.0;
        check_vector(j, "f98000");
        // must not be treated as positive zero
        CHECK(json::to_cbor_deterministic(j) != json::to_cbor_deterministic(json(0.0)));
        // the sign survives a round trip
        const auto parsed = json::from_cbor(json::to_cbor_deterministic(j));
        CHECK(parsed.is_number_float());
        CHECK(std::signbit(parsed.get<double>()));
    }

    SECTION("NaN and infinity use fixed encodings")
    {
        check_vector(json(std::numeric_limits<double>::quiet_NaN()), "f97e00");
        check_vector(json(-std::numeric_limits<double>::quiet_NaN()), "f97e00");
        check_vector(json(std::numeric_limits<double>::infinity()), "f97c00");
        check_vector(json(-std::numeric_limits<double>::infinity()), "f9fc00");
    }

    SECTION("output adapter overloads agree")
    {
        const json j = {{"b", 1}, {"aa", 2.5}};
        const auto v = json::to_cbor_deterministic(j);

        std::vector<std::uint8_t> v_adapter;
        json::to_cbor_deterministic(j, v_adapter);
        CHECK(v == v_adapter);

        std::vector<char> c_adapter;
        json::to_cbor_deterministic(j, c_adapter);
        CHECK(std::vector<std::uint8_t>(c_adapter.begin(), c_adapter.end()) == v);
    }

    SECTION("input value is not modified")
    {
        const json j = json::parse("{\"aa\":2,\"b\":[1.5,{\"x\":null}]}");
        const std::string before = j.dump();
        (void)json::to_cbor_deterministic(j);
        CHECK(j.dump() == before);
    }
}
