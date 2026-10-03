# Read-only structural comparison (`nlohmann::structural_compare`)

New header: `include/nlohmann/structural_compare.hpp`. It is independent of
`basic_json::operator==`, whose semantics are **unchanged** (it still performs
the library's usual numeric conversion). The new utility never converts
integers through `double`, so it is safe for configuration-audit style
comparisons of 64-bit values.

## API

```cpp
#include <nlohmann/structural_compare.hpp>

auto result = nlohmann::structural_compare(a, b);   // a, b: any basic_json type
result.equal;                    // bool
result.first_difference;         // json_pointer, "" == document root
result.first_difference.to_string();  // RFC 6901 escaped string

bool same = nlohmann::structural_equal(a, b);       // convenience wrapper
```

Both operands are taken by `const&` and are never modified (read-only).

## Comparison rules

- **Exact numeric comparison.** Signed integers, unsigned integers and
  floating-point numbers are compared by exact numeric value. Integers are
  never converted through `double`, so distinct integers beyond 2^53 stay
  distinct: `9007199254740992 != 9007199254740993`.
- **Integer vs float.** Equal only if the float is finite, integral, and
  exactly representable in the integer's domain: `1 == 1.0`, but
  `9007199254740993 != 9007199254740992.0`. Range checks use the exactly
  representable bound 2^digits, so negative integers vs. large unsigned
  values (e.g. `-1` vs. `18446744073709551615`) can neither overflow nor
  wrap around.
- **NaN policy: rejected.** NaN is never equal to any value, not even NaN.
  The comparison reports "different" with the pointer at the NaN location.
- **-0.0 policy.** `-0.0 == 0.0 == 0` (IEEE 754 semantics; signed zero is
  treated as equal to zero).
- **Infinity policy.** `+inf == +inf`, `-inf == -inf`; infinity is never
  equal to any finite value or integer; `+inf != -inf`.
- **No string coercion.** `"1" != 1` and `"1.0" != 1.0`; types must match
  exactly outside the numeric class. Booleans are not coerced either
  (`true != 1`).
- **Objects:** key order is insignificant. **Arrays:** element order is
  significant.
- **First difference** is reported as an RFC 6901 JSON Pointer with proper
  escaping (`~` -> `~0`, `/` -> `~1`), e.g. `/a~1b/~0key/2`.

## Build and run only this test

Standalone, no test framework needed (from the repository root):

```sh
g++ -std=c++11 -I include tests/structural_compare/structural_compare_test.cpp -o structural_compare_test
./structural_compare_test        # prints "<n> checks, 0 failures"
```

MSVC equivalent:

```sh
cl /std:c++14 /EHsc /I include tests/structural_compare/structural_compare_test.cpp
```
