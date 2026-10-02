# <small>nlohmann::basic_json::</small>to_cbor_deterministic

```cpp
// (1)
static std::vector<std::uint8_t> to_cbor_deterministic(const basic_json& j);

// (2)
static void to_cbor_deterministic(const basic_json& j, detail::output_adapter<std::uint8_t> o);
static void to_cbor_deterministic(const basic_json& j, detail::output_adapter<char> o);
```

Serializes a given JSON value `j` to a byte vector using a deterministic (canonical) CBOR encoding as defined in
[RFC 8949, Section 4.2](https://www.rfc-editor.org/rfc/rfc8949.html#section-4.2). The same JSON value always
serializes to the same byte sequence, which makes the output suitable for hashing and digital signatures.

1. Returns a byte vector containing the deterministic CBOR serialization.
2. Writes the deterministic CBOR serialization to an output adapter.

The deterministic encoding applies the following rules:

- Integers and length fields use the shortest possible encoding.
- Floating-point values use the shortest of half-, single-, or double-precision that represents the value
  losslessly. NaN is always encoded as `0xf97e00`. The sign of `-0.0` is preserved.
- Object keys are sorted by the length of their encoded form first, then bytewise lexicographically
  (length-first ordering). Keys are **not** merely sorted as JSON strings.
- Array order is preserved.

The input value is only read and never modified. [`to_cbor`](to_cbor.md) is unaffected by this function and keeps
its existing behavior.

## Parameters

`j` (in)
:   JSON value to serialize

`o` (in)
:   output adapter to write serialization to

## Return value

1. deterministic CBOR serialization as a byte vector
2. (none)

## Exception safety

Strong guarantee: if an exception is thrown, there are no changes in the JSON value.

## Complexity

Linear in the size of the JSON value `j`, plus the sorting of object keys.

## Examples

??? example

    The example shows that objects with the same keys in different insertion order serialize to the same
    deterministic CBOR byte vector.
     
    ```cpp
    --8<-- "examples/to_cbor_deterministic.cpp"
    ```
    
    Output:
    
    ```json
    --8<-- "examples/to_cbor_deterministic.output"
    ```

## See also

- [to_cbor](to_cbor.md) create a (non-deterministic) CBOR serialization of a JSON value
- [from_cbor](from_cbor.md) create a JSON value from an input in CBOR format

## Version history

- Added in version 3.12.0.
