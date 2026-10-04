//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | |   | |  version 3.12.0
// |_____|_____|_|_|_|___|_|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

/*
Streaming JSON diff demo.

Compares two JSON inputs token by token without building a DOM; memory use
is limited to the current nesting path and bounded token previews.

Build (from the repository root):

    g++ -std=c++11 -I include -o build/stream_diff_demo examples/stream_diff_demo.cpp

Usage:

    stream_diff_demo [fileA.json fileB.json]   # compare two files, streamed
    stream_diff_demo                           # run the built-in demo
*/

#include <nlohmann/json_diff.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <istream>
#include <streambuf>
#include <string>

namespace jd = nlohmann::json_diff;

static void print_result(const char* label, const jd::result& r)
{
    std::cout << label << "\n  outcome: " << jd::to_string(r.outcome) << "\n";
    switch (r.outcome)
    {
        case jd::status::different:
            std::cout << "  kind:        " << jd::to_string(r.kind) << "\n"
                      << "  pointer:     " << (r.json_pointer.empty() ? "<root>" : r.json_pointer) << "\n"
                      << "  lhs token:   " << r.lhs.preview
                      << " (line " << r.lhs.position.line
                      << ", column " << r.lhs.position.column
                      << ", offset " << r.lhs.position.offset << ")\n"
                      << "  rhs token:   " << r.rhs.preview
                      << " (line " << r.rhs.position.line
                      << ", column " << r.rhs.position.column
                      << ", offset " << r.rhs.position.offset << ")\n";
            break;
        case jd::status::syntax_error:
            std::cout << "  input " << r.error_side << " is not valid JSON\n"
                      << "  at: line " << (r.error_side == 1 ? r.lhs.position.line : r.rhs.position.line)
                      << ", column " << (r.error_side == 1 ? r.lhs.position.column : r.rhs.position.column)
                      << ", offset " << (r.error_side == 1 ? r.lhs.position.offset : r.rhs.position.offset) << "\n"
                      << "  " << r.message << "\n";
            break;
        case jd::status::depth_exceeded:
        case jd::status::equal:
            std::cout << "  " << (r.message.empty() ? "inputs are equal" : r.message) << "\n";
            break;
    }
    std::cout << std::endl;
}

// a streambuf that yields its input a few bytes at a time, like a slow
// network connection
class chunked_streambuf : public std::streambuf
{
  public:
    chunked_streambuf(const std::string& data, std::size_t chunk)
        : m_data(data), m_chunk(chunk)
    {}

  protected:
    int_type underflow() override
    {
        if (m_pos == m_data.size())
        {
            return traits_type::eof();
        }
        const std::size_t n = std::min(m_chunk, m_data.size() - m_pos);
        char* begin = const_cast<char*>(m_data.data()) + m_pos;
        setg(begin, begin, begin + n);
        m_pos += n;
        return traits_type::to_int_type(*gptr());
    }

  private:
    const std::string& m_data;
    std::size_t m_chunk;
    std::size_t m_pos = 0;
};

static int compare_files(const char* path_a, const char* path_b)
{
    // the files are streamed: they are never loaded into memory as a whole
    std::ifstream file_a(path_a);
    std::ifstream file_b(path_b);
    if (!file_a.good() || !file_b.good())
    {
        std::cerr << "cannot open input file(s)" << std::endl;
        return EXIT_FAILURE;
    }
    print_result("file comparison", jd::compare(file_a, file_b));
    return EXIT_SUCCESS;
}

static int run_demo()
{
    // 1. a single value changed deep inside nested arrays
    print_result("deep array value change",
                 jd::compare("[[[[1,2,3],[4,5,6]]]]", "[[[[1,2,3],[4,7,6]]]]"));

    // 2. object member order is significant
    print_result("member order change",
                 jd::compare("{\"alpha\":1,\"beta\":2}", "{\"beta\":2,\"alpha\":1}"));

    // 3. strings compare by decoded value
    const std::string bs(1, static_cast<char>(0x5C));
    print_result("equivalent escapes",
                 jd::compare("{\"k\":\"A\"}", "{\"k\":\"" + bs + "u0041\"}"));

    // 4. a syntax error anywhere wins over an earlier content difference
    print_result("syntax error priority",
                 jd::compare("{\"x\":1,\"y\":2}", "{\"x\":999,\"y\":2,}"));

    // 5. inputs that arrive a few bytes at a time
    const std::string a = "{\"menu\":{\"items\":[1,2,{\"id\":\"x\"}]}}";
    const std::string b = "{\"menu\":{\"items\":[1,2,{\"id\":\"y\"}]}}";
    chunked_streambuf buf_a(a, 3); // 3 bytes per read
    chunked_streambuf buf_b(b, 3);
    std::istream stream_a(&buf_a);
    std::istream stream_b(&buf_b);
    print_result("chunked input (3 bytes per read)", jd::compare(stream_a, stream_b));

    // 6. configurable nesting limit
    jd::options opt;
    opt.max_depth = 2;
    print_result("nesting limit (max_depth = 2)", jd::compare("[[[1]]]", "[[[1]]]", opt));

    return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
    if (argc == 3)
    {
        return compare_files(argv[1], argv[2]);
    }
    if (argc == 1)
    {
        return run_demo();
    }
    std::cerr << "usage: stream_diff_demo [fileA.json fileB.json]" << std::endl;
    return EXIT_FAILURE;
}
