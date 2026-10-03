#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main()
{
    // large integers beyond 2^53 must not become equal
    json a = 9007199254740992;
    json b = 9007199254740993;
    auto res = json::structural_compare(a, b);
    std::cout << std::boolalpha << "equal: " << res.equal
              << ", pointer: " << res.first_difference.to_string() << '\n';

    // integer 1 and float 1.0 are equal
    json c = 1;
    json d = 1.0;
    std::cout << "equal: " << json::structural_compare(c, d).equal << '\n';

    // string "1" is not equal to integer 1
    json e = 1;
    json f = "1";
    std::cout << "equal: " << json::structural_compare(e, f).equal << '\n';

    // nested difference with escaped pointer
    json g = {{"a/b", 1}, {"c~d", 2}};
    json h = {{"a/b", 1}, {"c~d", 3}};
    auto res2 = json::structural_compare(g, h);
    std::cout << "equal: " << res2.equal
              << ", pointer: " << res2.first_difference.to_string() << '\n';
}
