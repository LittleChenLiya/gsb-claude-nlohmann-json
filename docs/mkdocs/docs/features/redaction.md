# Streaming redaction

The `nlohmann::json_redactor` class is a streaming redaction transcoder: it reads JSON text with the SAX parser and
writes a complete, valid JSON document in which only the subtrees selected by exact
[JSON Pointer](json_pointer.md) rules are replaced by a given value. It is available since version 3.12.0 via
`#!cpp #include <nlohmann/json.hpp>`.

```cpp
--8<-- "examples/redactor_demo.cpp"
```

Output:

```json
--8<-- "examples/redactor_demo.output"
```

## Rules

Each `nlohmann::redaction_rule` pairs an exact [RFC 6901](https://tools.ietf.org/html/rfc6901) JSON Pointer with a
serialized JSON replacement value:

- The empty pointer `""` addresses the whole document; `~0` and `~1` escape `~` and `/` in object keys.
- Array indices are zero-based and always counted on the **original input** - replacing an element does not shift the
  indices of its siblings.
- When several rules nest, the outermost match wins: once a parent pointer matches, the whole subtree is replaced and
  rules pointing inside it are not evaluated.
- Replacements must be valid JSON text (checked with [`accept`](../api/basic_json/accept.md) when the rule is added);
  otherwise `other_error.652` is thrown.

## Guarantees

- **Only matched subtrees change.** All other values are forwarded as parsed: object member order is preserved and
  numbers keep the precision of the parser configuration (floating-point tokens are re-emitted verbatim, so no
  round-trip through `double` can lose digits). Whitespace is normalized to the compact form.
- **Replaced subtrees are still validated.** The SAX parser consumes the complete input, so a syntax error hiding
  inside a replaced subtree fails the transcoding with a `parse_error`. No DOM is built for replaced subtrees and no
  user callbacks fire for them.
- **All-or-nothing publishing.** `transcode` writes to a staging sink and `transcode_file` only renames the staged
  temporary file to the final path after the whole input was processed successfully. A corrupted input or a failing
  sink therefore never publishes a partial document, and a pre-existing output file is left untouched. The output is
  streamed through the staging file and is never buffered in memory as a whole.

## Complexity

Transcoding runs in **O(n)** time in the input size (plus an O(log r) rule lookup per value for r rules) and needs
**O(d + r)** memory, where d is the nesting depth of the document - independent of the document size. Replaced
subtrees are validated but neither stored nor copied, so gigabyte-sized documents can be redacted with constant
memory.

## Exceptions

- `parse_error` - the input is not valid JSON (also inside replaced subtrees), or a rule pointer has invalid syntax.
- `other_error.651` - writing to the sink failed.
- `other_error.652` - a rule's replacement is not valid JSON.
- `other_error.654` - the staging file cannot be opened.
- `other_error.655` - the staged document cannot be published to the output path.
