# Streaming JSON diff

`include/nlohmann/json_diff.hpp` is an optional, header-only add-on that
compares two JSON inputs **token by token** (SAX-style) without building a
DOM. Memory use is limited to the current nesting path and bounded token
previews, so arbitrarily large inputs can be compared.

```cpp
#include <nlohmann/json_diff.hpp>

nlohmann::json_diff::options opt;
opt.max_depth = 0;            // 0 = unlimited nesting
opt.max_preview_length = 64;  // token previews in the report are truncated

auto result = nlohmann::json_diff::compare(stream_a, stream_b, opt);
// result.outcome       -> equal / different / syntax_error / depth_exceeded
// result.kind          -> type_mismatch, value_mismatch, key_mismatch,
//                         object_size_mismatch, array_size_mismatch
// result.json_pointer  -> RFC 6901 pointer to the first difference
// result.lhs/rhs       -> token preview + line/column/offset of both sides
```

Comparison semantics:

- object member order is significant; array elements compare in order
- strings compare by decoded value (`"A"` equals `"\u0041"`)
- numbers use the library's usual equality rules after type parsing
  (`1` equals `1.0` equals `1e0`)
- after the first difference, both inputs are still read to the end:
  a syntax error on either side takes priority over the difference

## Test

Standalone, no test framework required (from the repository root):

```sh
g++ -std=c++11 -Wall -Wextra -I include -o build/unit_stream_diff tests/stream_diff/unit_stream_diff.cpp
./build/unit_stream_diff
```

## Demo

```sh
g++ -std=c++11 -I include -o build/stream_diff_demo examples/stream_diff_demo.cpp
./build/stream_diff_demo                          # built-in streaming demo
./build/stream_diff_demo a.json b.json            # compare two files, streamed
```
