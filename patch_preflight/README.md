# JSON Patch Preflight Checker

A header-only add-on for [JSON for Modern C++](https://github.com/nlohmann/json)
that **dry-runs a JSON Patch (RFC 6902) on an internal copy** of the target
document and reports, for every operation, whether it was `applied`,
`failed`, or `blocked` — before anyone touches the real document.

## Why

`nlohmann::json::patch()` either applies the whole patch or throws, leaving
you with an exception and no per-operation detail. The preflight checker
answers, up front and operation by operation:

- which operation fails, at which **operation index**, and **why**;
- whether the failing location is the operation's **`path`** or its
  **`from`** pointer (move/copy), with the raw JSON Pointer preserved;
- which operations never ran because an earlier failure made the document
  state uncertain (**blocked**);
- what the document would look like after the applied prefix of operations.

Because every operation is **genuinely executed** on the internal copy
(following the semantics of `basic_json::patch_inplace()`), a reported
`applied` means the operation really worked — including `test` comparisons
and the existence of the addressed values. This is not a syntax-only
validation: a `test` that would fail is reported as `failed`, and a `remove`
of a missing key is reported as `failed`, never as "looks valid".

## Guarantees

- **Inputs are never modified.** Neither the document nor the patch passed
  to `check()` is changed; all mutations happen on an internal copy that is
  returned as `report::document`.
- **Stop at first failure.** The first failed operation ends execution;
  every later operation is marked `blocked` and is *not* executed on the
  uncertain state. Blocked entries still carry the operation's `op`, `path`,
  and `from` identifiers for orientation, but claim nothing about validity.
- **Existing patch semantics.** `add`/`remove`/`replace`/`move`/`copy`/`test`
  behave exactly as in `basic_json::patch_inplace()`, including:
  - the empty pointer `""` addresses the document root (`add ""` replaces
    the whole document; `remove ""` fails, as the root has no parent);
  - JSON Pointer escaping (`~0` for `~`, `~1` for `/`) is honored, and the
    raw (escaped) pointer strings are kept in the report;
  - the array index `-` (append) is accepted **only** as the last token of
    an `add` path (including the target of `copy`/`move`, which are adds);
    it is rejected for `remove`/`replace`/`test`, as a `from` location, and
    in any non-final position;
  - `move` rejects a `from` that is a proper prefix of `path`
    (RFC 6902 section 4.4).

## API

```cpp
#include <nlohmann/json.hpp>
#include "json_patch_preflight.hpp"

namespace preflight = nlohmann::patch_preflight;

preflight::report rep = preflight::check(document, patch);

rep.ok;             // true iff every operation applied
rep.failed_index;   // index of the first failed operation (when !rep.ok)
rep.document;       // internal copy after the applied prefix
rep.operations;     // one op_report per patch operation, in order
```

Each `op_report` keeps:

| member      | meaning                                                        |
|-------------|----------------------------------------------------------------|
| `index`     | position of the operation within the patch                     |
| `op`        | the `op` member (`add`/`remove`/`replace`/`move`/`copy`/`test`)|
| `path`      | raw JSON Pointer of the `path` member (`""` = document root)   |
| `has_from` / `from` | raw JSON Pointer of the `from` member (move/copy)        |
| `status`    | `op_status::applied`, `op_status::failed`, `op_status::blocked`|
| `failed_at` | `pointer_role::path` or `pointer_role::from` — which pointer caused the failure |
| `message`   | diagnostic detail for failed operations                        |

Requires C++11 and exceptions.

## Example

```cpp
const nlohmann::json document = {{"a", "old"}, {"b", 1}, {"c", {1, 2, 3}}};
const nlohmann::json patch = nlohmann::json::array({
    {{"op", "replace"}, {"path", "/a"}, {"value", "new"}},
    {{"op", "test"},    {"path", "/b"}, {"value", 42}},
    {{"op", "remove"},  {"path", "/c"}}
});

const auto rep = nlohmann::patch_preflight::check(document, patch);
// rep.ok == false, rep.failed_index == 1
// operations[0]: replace /a       -> applied
// operations[1]: test    /b       -> failed   [path] value does not match
// operations[2]: remove  /c       -> blocked  (not executed)
// document["a"] is still "old" — only the internal copy was modified.
```

## Building the demo and the tests

From the repository root:

```sh
# demo: prints preflight reports for three scenarios
g++ -std=c++11 -I include patch_preflight/demo.cpp -o patch_preflight/demo
./patch_preflight/demo

# tests: acceptance scenario, root path, pointer escaping, "-" handling,
# input immutability, structural errors, genuine execution
g++ -std=c++11 -I include patch_preflight/test_preflight.cpp -o patch_preflight/test_preflight
./patch_preflight/test_preflight
```

## Files

- `json_patch_preflight.hpp` — the header-only implementation
- `demo.cpp` — demo program that prints preflight reports
- `test_preflight.cpp` — deterministic tests (no external test framework)
- `README.md` — this document
