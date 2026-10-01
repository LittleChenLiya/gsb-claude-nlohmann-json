# <small>nlohmann::basic_json::</small>keyed_diff

```cpp
static basic_json keyed_diff(const basic_json& source,
                             const basic_json& target,
                             const std::map<string_t, string_t>& array_id_fields,
                             const string_t& path = "");
```

Creates a [JSON Patch](http://jsonpatch.com) so that value `source` can be changed into the value `target` by
calling the [`patch`](patch.md) function, just like [`diff`](diff.md). In addition, arrays whose path occurs in
`array_id_fields` are compared **by element identity** instead of by position: each element must be an object, and
the value of the configured member (its _ID_) decides which source element corresponds to which target element.

For two JSON values `source` and `target`, the following code yields always `#!cpp true`:
```cpp
source.patch(keyed_diff(source, target, array_id_fields)) == target;
```

## Parameters

`source` (in)
:   JSON value to compare from

`target` (in)
:   JSON value to compare against

`array_id_fields` (in)
:   maps the JSON Pointer of an array (e.g. `"/items"`; `""` addresses the top-level value) to the name of the
    object member whose value identifies the elements of that array

`path` (in)
:   JSON Pointer of the values `source` and `target` within a larger document; used for recursion

## Return value

a JSON patch to convert the `source` to `target`

## Exception safety

Strong guarantee: if an exception is thrown, there are no changes in the JSON value.

## Exceptions

The identity data of every configured array is validated before any operation is generated. The whole diff is
rejected - there is **no fallback** to positional comparison, which could silently mismatch elements - when:

- an element is not an object or has no ID member
  ([json.exception.other_error.503](../../home/exceptions.md#jsonexceptionother_error503)),
- the ID value is neither a string nor an integer
  ([json.exception.other_error.504](../../home/exceptions.md#jsonexceptionother_error504)), or
- an ID occurs more than once in the same array
  ([json.exception.other_error.505](../../home/exceptions.md#jsonexceptionother_error505)).

## Complexity

Linear in the lengths of `source` and `target` for the comparison itself; matching the elements of a configured
array adds a factor linear in the length of the array.

## Notes

- IDs are compared as JSON values: the string `"1"` and the number `1` are **different** identities, whereas the
  integers `1` and `1u` denote the same identity.
- Arrays whose path is **not** configured keep the positional semantics of [`diff`](diff.md). The configuration is
  propagated to nested values, so arrays nested inside matched objects are also recognized by their path.
- The generated operations use a deterministic move strategy (it is not guaranteed to be the minimal number of
  operations):
    1. Elements whose ID does not occur in the target are removed first, highest index first.
    2. The target is then processed in order. A missing ID is added at its target index; an existing ID at a wrong
       position is relocated with a single `move` operation; the contents of matched objects are compared
       recursively at the position the element occupies after the move.
    3. All array indices in the generated operations refer to the array state produced by the operations before
       them, so applying the operations one by one with [`patch`](patch.md) always yields the `target`.

## Examples

??? example

    The following code shows how a JSON patch is created for an array whose objects are matched by their `"id"`
    member: reorderings become `move` operations, and content changes are applied at the moved-to position.

    ```cpp
    --8<-- "examples/keyed_diff.cpp"
    ```

    Output:

    ```json
    --8<-- "examples/keyed_diff.output"
    ```

## See also

- [diff](diff.md) creates a diff as a JSON patch using positional array comparison
- [RFC 6902 (JSON Patch)](https://tools.ietf.org/html/rfc6902)
- [patch](patch.md) applies a JSON Patch
- [patch_inplace](patch_inplace.md) applies a JSON Patch in place

## Version history

- Added in version 3.13.0.
