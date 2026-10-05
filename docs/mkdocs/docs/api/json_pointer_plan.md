# <small>nlohmann::</small>json_pointer_plan

```cpp
template<typename RefStringType>
class json_pointer_plan;
```

`json_pointer_plan` is an explicit, compiled plan to resolve several JSON pointers through one document. A plan is compiled from a list of [`json_pointer`](json_pointer/json_pointer.md) values. The compilation merges the common prefixes of the pointers into a trie, so that each JSON value along a shared prefix is accessed only once when the plan is executed.

Execution returns one result per request, in request order. Each result is an independently owned copy of the pointed-to value or a classified error. Executing a plan never modifies the document it is executed against.

## Template parameters

`RefStringType`
:    the string type used for reference tokens; defaults to `std::string`

## Member types

- `string_t` - the reference token string type
- `pointer_t` - the JSON pointer type (`json_pointer<RefStringType>`)
- `result` - the result of resolving a single request through the plan

## Member functions

- [`compile`](#compile) - compile a plan from a range of JSON pointers
- [`execute`](#execute) - execute the plan against a JSON document
- [`request_count`](#request_count) - the number of requests in the plan
- [`empty`](#empty) - whether the plan contains no requests

## Error classification

When a request cannot be resolved, the error is classified as one of the following:

| Error | Description |
|-------|-------------|
| `success` | the pointer resolved to a value |
| `missing_key` | an object did not contain the requested key |
| `wrong_container` | a non-null primitive value was encountered where descent was required |
| `array_out_of_range` | an array index was out of range or the array index was `-` |
| `null_container` | an existing null value was encountered where descent was required |
| `invalid_index` | an array index was malformed (parse error 106/109) |

Each failed result carries a `failed_prefix` - the pointer prefix that could not be resolved. All requests that fail at the same source share the same `failed_prefix`.

## Examples

??? example

    The example shows how to compile a plan from several JSON pointers, execute it against a document, and inspect the results.

    ```cpp
    --8<-- "examples/json_pointer_plan.cpp"
    ```

    Output:

    ```json
    --8<-- "examples/json_pointer_plan.output"
    ```

## See also

- [`json_pointer`](json_pointer/json_pointer.md) - JSON pointers as described in RFC 6901
- [`basic_json::at`](basic_json/at.md) - access a JSON value via a JSON pointer

## Version history

- Added in version 3.12.0.
