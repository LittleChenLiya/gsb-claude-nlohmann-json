# <small>nlohmann::basic_json::</small>to_cbor_deterministic

```cpp
// (1)
static std::vector<std::uint8_t> to_cbor_deterministic(const basic_json& j);

// (2)
static void to_cbor_deterministic(const basic_json& j, detail::output_adapter<std::uint8_t> o);
static void to_cbor_deterministic(const basic_json& j, detail::output_adapter<char> o);
```

Serializes a given JSON value `j` to a byte vector using a deterministic variant of the CBOR (Concise Binary Object
Representation) serialization format. The output is a fixed byte string for a given JSON value, so it can be used as
the input to a signature or hash.

1. Returns a byte vector containing the deterministic CBOR serialization.
2. Writes the deterministic CBOR serialization to an output adapter.

The serialization follows these rules:

- Maps are written with a definite length. Their keys are sorted by the length of their encoded key data item
  (shorter keys first), then by bytewise lexicographic order of the encoded key bytes. The sorting is performed on
  the encoded keys, not on the JSON key strings, so it does not depend on the order in which the keys were inserted.
- Integers and lengths use the shortest legal encoding.
- Floating-point numbers use the shortest format that can represent the value without loss (half-precision, then
  single-precision, then double-precision). NaN is encoded uniformly as half-precision quiet NaN (`0xf9 0x7e 0x00`),
  and the sign of `-0.0` is preserved.
- The order of array elements is preserved.

The exact mapping and its limitations are described on a [dedicated page](../../features/binary_formats/cbor.md).

## Parameters

`j` (in)
:   JSON value to serialize

`o` (in)
:   output adapter to write serialization to

## Return value

1. Deterministic CBOR serialization as a byte vector
2. (none)

## Exception safety

Strong guarantee: if an exception is thrown, there are no changes in the JSON value.

## Complexity

Linear in the size of the JSON value `j`.

## Examples

??? example

    The example shows that two JSON values with the same keys in different insertion order yield the same
    deterministic CBOR byte string. The key `b` (encoded length 2) sorts before `aa` (encoded length 3).

    ```cpp
    --8<-- "examples/to_cbor_deterministic.cpp"
    ```

    Output:

    ```json
    --8<-- "examples/to_cbor_deterministic.output"
    ```

## See also

- [to_cbor](to_cbor.md) create a CBOR serialization of a given JSON value
- [from_cbor](from_cbor.md) create a JSON value from an input in CBOR format
- [to_msgpack](to_msgpack.md) create a MessagePack serialization of a JSON value
- [to_bson](to_bson.md) create a BSON serialization of a JSON value
- [to_ubjson](to_ubjson.md) create a UBJSON serialization of a JSON value
- [to_bjdata](to_bjdata.md) create a BJData serialization of a JSON value

## Version history

- Added in version 3.12.0.
