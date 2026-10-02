#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

int main()
{
    // the same object with different key insertion orders
    ordered_json j1 = {{"b", 1}, {"aa", 2}};
    ordered_json j2 = {{"aa", 2}, {"b", 1}};

    // deterministic CBOR serializes both to the same byte vector:
    // keys are sorted by encoded length first ("b" before "aa")
    std::vector<std::uint8_t> v1 = json::to_cbor_deterministic(j1);
    std::vector<std::uint8_t> v2 = json::to_cbor_deterministic(j2);

    std::cout << std::boolalpha << (v1 == v2) << '\n';
    for (const auto byte : v1)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    std::cout << '\n';

    // 1.5 is exactly representable as a half-precision float (0xf93e00)
    std::vector<std::uint8_t> v3 = json::to_cbor_deterministic(json(1.5));
    for (const auto byte : v3)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    std::cout << '\n';
}
