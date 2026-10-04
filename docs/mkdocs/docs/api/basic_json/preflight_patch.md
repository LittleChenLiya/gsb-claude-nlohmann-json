# <small>nlohmann::basic_json::</small>preflight_patch

```cpp
json_patch_preflight_report preflight_patch(const basic_json& json_patch) const;
```

Preflights a [JSON Patch](http://jsonpatch.com) against the current JSON value
without modifying the document or the patch. The preflight applies each
operation to an internal copy of the document and reports, for every
operation, whether it would succeed, fail, or be blocked because an earlier
operation failed.

Unlike [patch](patch.md), which either applies all operations or throws an
exception, `preflight_patch` never throws due to a failing operation. Instead,
it records the failure and continues to the next operation. If an operation
fails, all subsequent operations are marked `blocked` and are not checked,
because the state of the internal copy is uncertain.

The preflight does not just validate the patch syntax: it actually attempts
each operation on the internal copy, so failures such as a missing target
location or an unsuccessful `test` operation are detected and reported.

## Parameters

`json_patch` (in)
:   JSON patch document

## Return value

A `json_patch_preflight_report`
with one entry per operation, in order. Each entry contains:

- `index`: the position of the operation in the patch array
- `op`: the operation name (`"add"`, `"remove"`, `"replace"`, `"move"`,
  `"copy"`, or `"test"`)
- `path`: the JSON Pointer from the operation's `path` member
- `from`: the JSON Pointer from the operation's `from` member (empty if absent)
- `status`: one of `ok`, `failed`, or `blocked`
- `message`: a human-readable diagnostic message (empty if `status` is `ok`)

The report also has an `ok()` method that returns `true` if every operation
would succeed.

## Exception safety

Strong guarantee: the original document and the patch are never modified.

## Complexity

Linear in the size of the JSON value and the length of the JSON patch.

## Notes

- The preflight applies operations to an internal copy of the document. The
  original document is never modified.
- If an operation fails, all subsequent operations are marked `blocked` and
  are not checked, because the state of the internal copy is uncertain.
- The `message` field of a failed operation contains the same diagnostic that
  [patch](patch.md) would throw as an exception.
- For `move` and `copy` operations, the `from` field distinguishes the source
  pointer from the `path` field. If the `from` location does not exist, the
  diagnostic message indicates that the failure is on the `from` path.
- The `-` character (which means "append" for arrays) is only valid for `add`
  operations. It is not accepted as an array index for `remove`, `replace`,
  `test`, or the `from` member of `move`/`copy` operations.

## Examples

??? example

    The following code shows how a JSON patch is preflighted against a value.

    ```cpp
    --8<-- "examples/preflight_patch.cpp"
    ```

    Output:

    ```text
    --8<-- "examples/preflight_patch.output"
    ```

## See also

- [RFC 6902 (JSON Patch)](https://tools.ietf.org/html/rfc6902)
- [RFC 6901 (JSON Pointer)](https://tools.ietf.org/html/rfc6901)
- [patch](patch.md) applies a JSON Patch to a copy of the document
- [patch_inplace](patch_inplace.md) applies a JSON Patch without creating a copy of the document
- [merge_patch](merge_patch.md) applies a JSON Merge Patch

## Version history

- Added in version 3.12.0.
