#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main()
{
    // two JSON values with the same keys in different order
    json j1 = json::object();
    j1["b"] = 1;
    j1["aa"] = 2;

    json j2 = json::object();
    j2["aa"] = 2;
    j2["b"] = 1;

    // serialize both to deterministic CBOR
    std::vector<std::uint8_t> v1 = json::to_cbor_deterministic(j1);
    std::vector<std::uint8_t> v2 = json::to_cbor_deterministic(j2);

    // the key "b" (encoded length 2) sorts before "aa" (encoded length 3),
    // regardless of the insertion order
    std::cout << std::hex << std::setfill('0');
    for (const auto& v : {v1, v2})
    {
        for (auto& byte : v)
        {
            std::cout << "0x" << std::setw(2) << static_cast<int>(byte) << " ";
        }
        std::cout << std::endl;
    }
}
