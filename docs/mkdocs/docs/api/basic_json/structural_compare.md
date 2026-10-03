# <small>nlohmann::basic_json::</small>structural_compare

```cpp
struct structural_compare_result
{
    bool equal = true;
    json_pointer first_difference;
};

static structural_compare_result structural_compare(const_reference lhs, const_reference rhs);
```

Performs a read-only structural comparison of two JSON values. Unlike
[`operator==`](operator_eq.md), which follows the library's legacy comparison
semantics, this function compares values by their exact structural and numeric
value and returns a pointer to the first difference.

The comparison does not modify the input values.

## Comparison rules

### Numbers

Signed integers, unsigned integers, and floating-point numbers are compared by
their exact numeric value:

- Two integers are compared directly; large integers (beyond 2^53) are
  **not** converted to floating-point before comparison, so distinct integers
  never compare equal.
- An integer and a floating-point number are compared without precision loss:
  the float is truncated to an integer only when it is within the integer's
  exact representable range.
- Two floating-point numbers are compared by their exact value. `-0.0` compares
  equal to `0.0` (standard IEEE semantics).

!!! warning "NaN and infinity"

    NaN and infinity are **rejected** and cause a `type_error` (error code 318)
    to be thrown. NaN cannot be meaningfully compared, and infinity is not a
    valid JSON value.

### Objects

Object key order does not affect the result. Keys are matched by exact string
comparison. If a key is present in one object but missing in the other, the
values are not equal and the pointer points to the missing key.

### Arrays

Array order is preserved: elements are compared index by index. If two arrays
have different lengths, they are not equal and the pointer points to the array
itself (the parent path).

### Strings

Strings are compared by exact value. String numbers (e.g., `"1"`) are **not**
automatically converted to numbers.

### Other types

Booleans, null, and binary values are compared by exact value. Discarded values
are not comparable.

## Parameters

`lhs` (in)
:   the first JSON value to compare

`rhs` (in)
:   the second JSON value to compare

## Return value

A `structural_compare_result` containing:

- `equal`: `#!cpp true` if the values are structurally equal, `#!cpp false`
  otherwise.
- `first_difference`: a [JSON Pointer](../json_pointer/index.md) to the first
  difference. Valid only when `equal` is `#!cpp false`. The pointer is empty
  when the difference is at the root level (e.g., two values of different
  types, or two arrays of different lengths).

## Exception safety

Strong guarantee: if an exception is thrown, there are no changes to the JSON
values.

## Exceptions

Throws [`type_error`](exception.md) (error code 318) if either operand is NaN or
infinity.

## Complexity

Linear in the size of the values.

## Examples

??? example

    The following code shows how to use `structural_compare` to compare two
    JSON values and inspect the first difference.

    ```cpp
    --8<-- "examples/structural_compare.cpp"
    ```

    Output:

    ```json
    --8<-- "examples/structural_compare.output"
    ```

## See also

- [operator==](operator_eq.md) compares two JSON values (legacy semantics)
- [diff](diff.md) creates a JSON Patch to transform one value into another
- [JSON Pointer](../json_pointer/index.md) for the pointer format

## Version history

- Added in version 3.12.0.
