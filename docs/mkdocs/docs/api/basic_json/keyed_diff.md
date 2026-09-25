# <small>nlohmann::basic_json::</small>keyed_diff

```cpp
static basic_json keyed_diff(const basic_json& source,
                             const basic_json& target,
                             const std::map<string_t, string_t>& array_id_keys = {},
                             const string_t& path = "");
```

Creates a [JSON Patch](http://jsonpatch.com) so that value `source` can be changed into the value `target` by calling
[`patch`](patch.md) function. It behaves exactly like [`diff`](diff.md), except that arrays whose JSON Pointer is
listed in `array_id_keys` are compared **by identity** instead of by position: elements of such arrays must be
objects, and two elements with the same id (the value of the configured member) are considered the same element.

For two JSON values `source` and `target`, the following code yields always `#!cpp true`:
```cpp
source.patch(keyed_diff(source, target, array_id_keys)) == target;
```

Arrays without a configured id key keep the positional semantics of [`diff`](diff.md).

## Parameters

`source` (in)
:   JSON value to compare from

`target` (in)
:   JSON value to compare against

`array_id_keys` (in)
:   maps the JSON Pointer of an array (e.g. `"/items"`, or `""` for the root value) to the name of the object
    member that uniquely identifies the elements of that array

`path` (in)
:   JSON Pointer of the values to compare; used for the recursive calls

## Return value

a JSON patch to convert the `source` to `target`

## Exception safety

Strong guarantee: if an exception is thrown, there are no changes in the JSON value.

## Exceptions

The whole diff is rejected (no positional fallback) with a
[`type_error.318`](../../home/exceptions.md#jsonexceptiontype_error318) if for a configured array

- an element is not an object,
- an element has no member with the configured id key,
- an id is neither a string nor a number, or
- an id occurs more than once.

## Complexity

Linear in the lengths of `source` and `target`, except for keyed arrays which may require a quadratic number of
element comparisons to compute the moves.

## Notes

Ids are compared by value and type: the number `1` and the string `"1"` are different ids, whereas the numbers
`1`, `1u`, and `1.0` are equal.

The generated operations for a keyed array are deterministic and follow a fixed strategy:

1. **Removals.** Elements whose id does not occur in the target are removed first, highest source index first, so
   that earlier removals do not invalidate the indices of later ones.
2. **Moves and adds.** The target positions are then processed left to right. An element that already exists is
   brought into place with a `move` operation; an element with a new id is inserted with an `add` operation
   carrying the full target value. Every index is computed against the current state of the array, so the effects
   of all previously emitted operations are already accounted for. Moves are preferred over remove/add pairs for
   existing elements, but the result is not guaranteed to be a minimal operation sequence.
3. **Content diffs.** Finally, matched elements are compared recursively. Because every element already sits at
   its target index at this point, all generated paths are expressed in target indices.

## Examples

??? example

    The following code shows how a JSON patch is created for an array of objects that are matched by their
    `"id"` member.
     
    ```cpp
    --8<-- "examples/keyed_diff.cpp"
    ```
    
    Output:
    
    ```json
    --8<-- "examples/keyed_diff.output"
    ```

## See also

- [diff](diff.md) creates a diff as a JSON patch using positional array comparison
- [patch](patch.md) applies a JSON Patch
- [RFC 6902 (JSON Patch)](https://tools.ietf.org/html/rfc6902)

## Version history

- Added in version 3.12.0.
