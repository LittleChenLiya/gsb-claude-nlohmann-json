# Streaming Redaction

The header `<nlohmann/redact.hpp>` provides a streaming redaction transcoder: it reads a JSON document with the [SAX parser](parsing/sax_interface.md) and writes a complete, valid JSON document in which exactly the values addressed by configured [JSON Pointers](json_pointer.md) are replaced by fixed replacement values. Everything else is echoed preserving order and semantics.

The header is self-contained on top of the public API of `<nlohmann/json.hpp>` and is not part of the amalgamated `json.hpp`. It requires C++ exceptions.

??? example

    ```cpp
    --8<-- "examples/redaction.cpp"
    ```

    Output:

    ```
    --8<-- "examples/redaction.output"
    ```

## Rules

- A rule (`nlohmann::redaction_rule`) pairs an exact JSON Pointer (RFC 6901, including `~0`/`~1` escapes) with a replacement `json` value. The empty pointer `""` addresses the whole document.
- Array indices always refer to the positions in the **original** input, independent of other replacements.
- Once a parent path matched, its whole subtree is replaced and no child rules are evaluated inside it.
- Replaced subtrees are still fully parsed and syntactically validated — a syntax error hidden inside a replaced subtree makes the transcription fail — but no DOM is built and no user callback is invoked for them.
- Numbers of unmatched values keep the precision of the parser's number types; floating-point tokens are echoed verbatim.
- If several rules carry the same pointer, the last one wins.

## Atomic publication

All output is written to a staging `nlohmann::redaction_sink`. Only after the complete input document was consumed and validated, `commit()` publishes the staged output. On any failure — a parse error anywhere in the input, an I/O error, or an exception — `abort()` is called instead and a half-written document is never published.

`nlohmann::redaction_file_sink` stages into a temporary file next to the target path and publishes by atomically renaming it over the target. The output document is never held in memory as a whole, so arbitrarily large documents can be transcoded. Custom sinks can be implemented by deriving from `nlohmann::redaction_sink`.

`nlohmann::json_redactor::transcribe` returns `true` iff the input was valid and the redacted document was published.

## Resource complexity

- **Time:** O(n) in the number of input tokens (a single SAX pass).
- **Memory:** O(d + r), where d is the nesting depth of the input and r is the total size of the configured rules. The memory footprint is independent of the document size.
- **Disk:** O(m) for the staging sink, where m is the size of the redacted output document.
