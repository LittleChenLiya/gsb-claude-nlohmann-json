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
// encode a byte vector as a lowercase hexadecimal string
std::string to_hex(const std::vector<std::uint8_t>& v)
{
    std::string result;
    result.reserve(v.size() * 2);
    static const char* const digits = "0123456789abcdef";
    for (const auto byte : v)
    {
        result += digits[(byte >> 4) & 0x0f];
        result += digits[byte & 0x0f];
    }
    return result;
}

void print_hex(const char* label, const std::vector<std::uint8_t>& v)
{
    std::cout << label << ": " << to_hex(v) << '\n';
}
} // namespace

TEST_CASE("deterministic CBOR")
{
    SECTION("map keys are sorted by encoded length then byte order")
    {
        // keys "b" (encoded length 2) and "aa" (encoded length 3) must be
        // emitted in the same order regardless of the insertion order
        const auto expected = std::vector<std::uint8_t>
        {
            0xa2,             // map(2)
            0x61, 0x62, 0x01, // key "b" (text(1)), value 1
            0x62, 0x61, 0x61, 0x02 // key "aa" (text(2)), value 2
        };

        SECTION("std::map, insertion order [b, aa]")
        {
            json j = json::object();
            j["b"] = 1;
            j["aa"] = 2;
            const auto result = json::to_cbor_deterministic(j);
            print_hex("std::map order [b, aa]", result);
            CHECK(result == expected);
        }

        SECTION("std::map, insertion order [aa, b]")
        {
            json j = json::object();
            j["aa"] = 2;
            j["b"] = 1;
            const auto result = json::to_cbor_deterministic(j);
            print_hex("std::map order [aa, b]", result);
            CHECK(result == expected);
        }

        SECTION("ordered_map, insertion order [b, aa]")
        {
            ordered_json j = ordered_json::object();
            j["b"] = 1;
            j["aa"] = 2;
            const auto result = ordered_json::to_cbor_deterministic(j);
            print_hex("insertion order [b, aa]", result);
            CHECK(result == expected);
        }

        SECTION("ordered_map, insertion order [aa, b]")
        {
            ordered_json j = ordered_json::object();
            j["aa"] = 2;
            j["b"] = 1;
            const auto result = ordered_json::to_cbor_deterministic(j);
            print_hex("insertion order [aa, b]", result);
            CHECK(result == expected);
        }

        // the legacy to_cbor output is unchanged (keys in std::map order:
        // "aa" before "b")
        SECTION("legacy to_cbor is unchanged")
        {
            json j = json::object();
            j["b"] = 1;
            j["aa"] = 2;
            const auto legacy = json::to_cbor(j);
            print_hex("legacy to_cbor", legacy);
            CHECK(legacy == std::vector<std::uint8_t>
            {
                0xa2,
                0x62, 0x61, 0x61, 0x02,
                0x61, 0x62, 0x01
            });
        }
    }

    SECTION("map keys with the same encoded length are sorted by encoded bytes")
    {
        // keys "aa", "ab", "ba" all have encoded length 3; they sort by
        // their encoded bytes: 0x61 0x61 ... < 0x61 0x62 ... < 0x62 0x61 ...
        ordered_json j = ordered_json::object();
        j["ba"] = 3;
        j["ab"] = 2;
        j["aa"] = 1;
        const auto result = ordered_json::to_cbor_deterministic(j);
        print_hex("same-length keys", result);
        CHECK(result == std::vector<std::uint8_t>
        {
            0xa3,
            0x62, 0x61, 0x61, 0x01, // key "aa", value 1
            0x62, 0x61, 0x62, 0x02, // key "ab", value 2
            0x62, 0x62, 0x61, 0x03  // key "ba", value 3
        });
    }

    SECTION("arrays keep their element order")
    {
        const json j = {3, 1, 2, 0};
        const auto result = json::to_cbor_deterministic(j);
        print_hex("array", result);
        CHECK(result == std::vector<std::uint8_t>
        {
            0x84,             // array(4)
            0x03, 0x01, 0x02, 0x00
        });
    }

    SECTION("nested objects are sorted at every level")
    {
        ordered_json j = ordered_json::object();
        j["z"] = ordered_json
        {
            {"b", 1},
            {"aa", 2}
        };
        j["a"] = ordered_json::array(
        {
            3,
            ordered_json
            {
                {"y", 1},
                {"x", 2}
            }
        });
        const auto result = ordered_json::to_cbor_deterministic(j);
        print_hex("nested", result);
        CHECK(result == std::vector<std::uint8_t>
        {
            0xa2,                               // map(2)
            0x61, 0x61,                         // key "a"
            0x82, 0x03,                         // array(2): 3,
            0xa2,                               //   map(2)
            0x61, 0x78, 0x02,                   //     key "x", value 2
            0x61, 0x79, 0x01,                   //     key "y", value 1
            0x61, 0x7a,                         // key "z"
            0xa2,                               // map(2)
            0x61, 0x62, 0x01,                   //   key "b" (text(1)), value 1
            0x62, 0x61, 0x61, 0x02              //   key "aa" (text(2)), value 2
        });
    }

    SECTION("floating-point numbers use the shortest lossless format")
    {
        SECTION("1.5 is exactly representable in half precision")
        {
            const auto result = json::to_cbor_deterministic(json(1.5));
            print_hex("1.5", result);
            CHECK(result == std::vector<std::uint8_t>{0xf9, 0x3e, 0x00});
        }

        SECTION("0.1 is not exactly representable in half or single precision")
        {
            const auto result = json::to_cbor_deterministic(json(0.1));
            print_hex("0.1", result);
            CHECK(result == std::vector<std::uint8_t>
            {
                0xfb, 0x3f, 0xb9, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a
            });
        }

        SECTION("0.1f is exactly representable in single precision")
        {
            const auto result = json::to_cbor_deterministic(json(static_cast<double>(0.1f)));
            print_hex("0.1f", result);
            CHECK(result == std::vector<std::uint8_t>{0xfa, 0x3d, 0xcc, 0xcc, 0xcd});
        }

        SECTION("65504.0 is the largest finite half")
        {
            const auto result = json::to_cbor_deterministic(json(65504.0));
            print_hex("65504.0", result);
            CHECK(result == std::vector<std::uint8_t>{0xf9, 0x7b, 0xff});
        }

        SECTION("65505.0 is not representable in half precision but exactly in single precision")
        {
            // 65505.0 exceeds the largest finite half (65504.0), so it must
            // not be truncated to half; it is exactly representable as a
            // single-precision float
            const auto result = json::to_cbor_deterministic(json(65505.0));
            print_hex("65505.0", result);
            CHECK(result == std::vector<std::uint8_t>{0xfa, 0x47, 0x7f, 0xe1, 0x00});
        }
    }

    SECTION("the sign of zero is preserved")
    {
        SECTION("+0.0")
        {
            const auto result = json::to_cbor_deterministic(json(0.0));
            print_hex("+0.0", result);
            CHECK(result == std::vector<std::uint8_t>{0xf9, 0x00, 0x00});
        }

        SECTION("-0.0")
        {
            const auto result = json::to_cbor_deterministic(json(-0.0));
            print_hex("-0.0", result);
            CHECK(result == std::vector<std::uint8_t>{0xf9, 0x80, 0x00});
        }
    }

    SECTION("NaN uses a unified encoding")
    {
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        const auto result = json::to_cbor_deterministic(json(nan));
        print_hex("NaN", result);
        CHECK(result == std::vector<std::uint8_t>{0xf9, 0x7e, 0x00});
    }

    SECTION("infinity uses half precision")
    {
        const auto inf = std::numeric_limits<double>::infinity();
        CHECK(json::to_cbor_deterministic(json(inf)) == std::vector<std::uint8_t>{0xf9, 0x7c, 0x00});
        CHECK(json::to_cbor_deterministic(json(-inf)) == std::vector<std::uint8_t>{0xf9, 0xfc, 0x00});
    }

    SECTION("integer boundaries use the shortest encoding")
    {
        SECTION("positive integers")
        {
            CHECK(json::to_cbor_deterministic(json(23)) == std::vector<std::uint8_t>{0x17});
            CHECK(json::to_cbor_deterministic(json(24)) == std::vector<std::uint8_t>{0x18, 0x18});
            CHECK(json::to_cbor_deterministic(json(255)) == std::vector<std::uint8_t>{0x18, 0xff});
            CHECK(json::to_cbor_deterministic(json(256)) == std::vector<std::uint8_t>{0x19, 0x01, 0x00});
            CHECK(json::to_cbor_deterministic(json(65535)) == std::vector<std::uint8_t>{0x19, 0xff, 0xff});
            CHECK(json::to_cbor_deterministic(json(65536)) == std::vector<std::uint8_t>{0x1a, 0x00, 0x01, 0x00, 0x00});
            CHECK(json::to_cbor_deterministic(json(4294967295u)) == std::vector<std::uint8_t>{0x1a, 0xff, 0xff, 0xff, 0xff});
            CHECK(json::to_cbor_deterministic(json(4294967296u)) == std::vector<std::uint8_t>{0x1b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00});
        }

        SECTION("negative integers")
        {
            CHECK(json::to_cbor_deterministic(json(-24)) == std::vector<std::uint8_t>{0x37});
            CHECK(json::to_cbor_deterministic(json(-25)) == std::vector<std::uint8_t>{0x38, 0x18});
            CHECK(json::to_cbor_deterministic(json(-256)) == std::vector<std::uint8_t>{0x38, 0xff});
            CHECK(json::to_cbor_deterministic(json(-257)) == std::vector<std::uint8_t>{0x39, 0x01, 0x00});
            CHECK(json::to_cbor_deterministic(json(-65536)) == std::vector<std::uint8_t>{0x39, 0xff, 0xff});
            CHECK(json::to_cbor_deterministic(json(-65537)) == std::vector<std::uint8_t>{0x3a, 0x00, 0x01, 0x00, 0x00});
            CHECK(json::to_cbor_deterministic(json(-4294967296LL)) == std::vector<std::uint8_t>{0x3a, 0xff, 0xff, 0xff, 0xff});
            CHECK(json::to_cbor_deterministic(json(-4294967297LL)) == std::vector<std::uint8_t>{0x3b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00});
        }
    }

    SECTION("the input is not modified")
    {
        ordered_json j = ordered_json::object();
        j["b"] = 1;
        j["aa"] = 2;
        const auto before = j.dump();
        const auto legacy_before = ordered_json::to_cbor(j);
        static_cast<void>(ordered_json::to_cbor_deterministic(j));
        CHECK(j.dump() == before);
        CHECK(ordered_json::to_cbor(j) == legacy_before);
    }
}
