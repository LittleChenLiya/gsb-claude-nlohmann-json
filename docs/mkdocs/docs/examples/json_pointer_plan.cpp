#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using nlohmann::json_pointer_plan;
using nlohmann::json_pointer_error_t;

int main()
{
    // a JSON document
    json doc =
    {
        {"a", {{"x", 1}, {"y", 2}}},
        {"b", nullptr},
        {"c", json::array({10, 20, 30})}
    };

    // compile a plan from several JSON pointers; common prefixes are merged
    json_pointer_plan<std::string> plan = json_pointer_plan<std::string>::compile({
        json::json_pointer("/a/x"),   // success
        json::json_pointer("/a/y"),   // success
        json::json_pointer("/b"),     // null terminal, success
        json::json_pointer("/c/0"),   // success
        json::json_pointer("/c/9"),   // array out of range
        json::json_pointer("/d/x")    // missing key
    });

    std::cout << "plan contains " << plan.request_count() << " requests\n\n";

    // execute the plan; each request returns one result in request order
    std::vector<json_pointer_plan<std::string>::result<json>> results = plan.execute(doc);

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const auto& r = results[i];
        std::cout << "request " << i << ": ";

        if (r.ok())
        {
            std::cout << "success, value = " << r.value.dump() << '\n';
        }
        else
        {
            std::cout << "error = ";
            switch (r.error)
            {
                case json_pointer_error_t::missing_key:
                    std::cout << "missing_key";
                    break;
                case json_pointer_error_t::wrong_container:
                    std::cout << "wrong_container";
                    break;
                case json_pointer_error_t::array_out_of_range:
                    std::cout << "array_out_of_range";
                    break;
                case json_pointer_error_t::null_container:
                    std::cout << "null_container";
                    break;
                case json_pointer_error_t::invalid_index:
                    std::cout << "invalid_index";
                    break;
                default:
                    std::cout << "unknown";
                    break;
            }
            std::cout << ", failed_prefix = " << r.failed_prefix.to_string() << '\n';
        }
    }

    return 0;
}
