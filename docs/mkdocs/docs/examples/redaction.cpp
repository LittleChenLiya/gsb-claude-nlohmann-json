#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>
#include <nlohmann/redact.hpp>

namespace
{
std::string read_file(const std::string& path)
{
    std::ifstream stream(path.c_str());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}
} // namespace

int main()
{
    const std::string input =
        R"({"users":[{"name":"alice","token":"t0"},{"name":"bob","token":{"secret":[1,2,3]}}],"version":3})";

    // 1. redact /users/1/token and publish the result to redacted.json
    {
        nlohmann::redaction_file_sink sink("redacted.json");
        nlohmann::json_redactor redactor({{"/users/1/token", "***"}}, sink);
        if (redactor.transcribe(input))
        {
            std::cout << "redacted: " << read_file("redacted.json") << '\n';
        }
    }

    // 2. corrupted input (the syntax error hides inside the replaced
    //    subtree): transcribe fails and nothing is published
    {
        nlohmann::redaction_file_sink sink("redacted2.json");
        nlohmann::json_redactor redactor({{"/users/1/token", "***"}}, sink);
        const std::string broken =
            R"({"users":[{"name":"alice"},{"token":{"secret":]}}]})";
        const bool published = redactor.transcribe(broken);
        std::cout << std::boolalpha
                  << "corrupted input published: " << published << '\n';
    }

    // clean up the files created above
    std::remove("redacted.json");
    std::remove("redacted2.json");
}
