#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

int main()
{
    // replace /users/1/token with "***" and keep everything else as is
    nlohmann::json_redactor redactor({{"/users/1/token", "\"***\""}});

    // normal redaction: the staged output is published to redacted.json
    std::istringstream input(
        R"({"users":[{"name":"alice","token":"t0"},{"id":1,"token":"secret","keep":true}]})");
    redactor.transcode_file(input, "redacted.json");
    std::cout << "redacted: " << std::ifstream("redacted.json").rdbuf() << '\n';

    // corrupted JSON hiding inside the replaced token subtree: nothing is published
    std::remove("redacted.json");
    std::istringstream broken(R"({"users":[{"name":"alice"},{"token": {"broken": }}]})");
    try
    {
        redactor.transcode_file(broken, "redacted.json");
    }
    catch (const nlohmann::json::parse_error& e)
    {
        std::cout << "error: " << e.what() << '\n';
    }
    std::cout << "published: " << std::boolalpha
              << static_cast<bool>(std::ifstream("redacted.json")) << '\n';
}
