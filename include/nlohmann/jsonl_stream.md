# JSONL Stream Decoder

A streaming decoder for JSON Lines (JSONL / newline-delimited JSON) that reads
input in small byte chunks and delivers one parsed JSON value per physical
line. It reuses the existing `json` parser (`json::parse`) for each complete
record; it never reads the whole input into memory at once.

## Module

- Header: `include/nlohmann/jsonl_stream.hpp`
- Class: `nlohmann::jsonl_stream_decoder`
- Config: `nlohmann::jsonl_decoder_config`

## Usage

```cpp
#include <nlohmann/jsonl_stream.hpp>

using nlohmann::jsonl_stream_decoder;
using nlohmann::jsonl_decoder_config;

jsonl_decoder_config cfg;
cfg.max_record_bytes = 1024 * 1024;  // 1 MiB per record (0 = unlimited)
cfg.skip_blank_lines = true;         // skip empty lines

jsonl_stream_decoder decoder(cfg);

// feed chunks as they arrive
decoder.feed(chunk1.data(), chunk1.size());
decoder.feed(chunk2.data(), chunk2.size());
// ...

// signal end of stream (parses a final record without trailing newline)
decoder.finish();

// access delivered records
for (const auto& record : decoder.records()) {
    // process record
}

// check for errors
if (decoder.errored()) {
    std::cerr << "Error at record " << decoder.error_record()
              << " (byte offset " << decoder.error_byte_offset() << "): "
              << decoder.error_message() << "\n";
}
```

## API

### `jsonl_decoder_config`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_record_bytes` | `std::size_t` | `0` (unlimited) | Maximum bytes per record. Counted in **bytes**, not characters. |
| `skip_blank_lines` | `bool` | `true` | Whether to skip blank (whitespace-only) lines. |

### `jsonl_stream_decoder`

| Method | Description |
|--------|-------------|
| `feed(data, size)` | Feed a chunk of bytes. Returns the number of records delivered. |
| `feed(std::string)` | Feed a chunk (string overload). |
| `finish()` | Signal end of stream. Parses any buffered bytes as the final record. |
| `errored()` | Returns `true` if the decoder entered a permanent error state. |
| `error_record()` | Returns the 1-based record number where the error occurred (0 if no error). |
| `error_byte_offset()` | Returns the absolute byte offset (from stream start) of the error record's first byte. |
| `error_message()` | Returns the human-readable error message. |
| `records()` | Returns the delivered JSON values, in order. |
| `record_count()` | Returns the number of records delivered so far. |
| `bytes_fed()` | Returns the total number of bytes fed via `feed`. |

## Design

### Record boundaries

A record is terminated by a physical line ending:

- `\n` (LF) — 1 byte
- `\r\n` (CRLF) — 2 bytes
- a lone `\r` (CR) — 1 byte

An escaped newline inside a JSON string (the two-character sequence `\n`,
i.e. backslash followed by the letter `n`) is **not** a line ending. Since that
escape sequence contains no raw newline byte, it is never mistaken for a record
boundary.

### Chunked UTF-8

Multi-byte UTF-8 characters may be split across chunk boundaries. The decoder
buffers bytes until a complete record is available, so split UTF-8 sequences
are reassembled before parsing. A UTF-8 character split at EOF (incomplete
sequence) is rejected by the parser as an error.

### Byte limit

`max_record_bytes` is counted in **bytes**, not characters. A multi-byte
UTF-8 character counts as 3 or 4 bytes, exactly as many as it occupies on the
wire. The limit applies to the record content (excluding the trailing line
ending).

### Error handling

Once a record fails to parse or exceeds the byte limit, the decoder enters a
**permanent error state**: subsequent `feed` calls return `0` and do not
silently recover. Records delivered before the error remain valid and
accessible via `records()`.

The error is reported with:

- `error_record()` — the 1-based record number where the error occurred
- `error_byte_offset()` — the absolute byte offset (from stream start) of the
  error record's first byte
- `error_message()` — a human-readable error message (from the parser)

### Blank lines

When `skip_blank_lines` is `true` (the default), blank lines (lines containing
only spaces, tabs and carriage returns) are silently skipped and do not produce
a record. When `false`, a blank line is parsed as JSON (which fails, since an
empty string is not valid JSON).

## Demo

See `demo/jsonl_demo.cpp` for a short chunked demonstration.

```bash
g++ -std=c++17 -I../include demo/jsonl_demo.cpp -o jsonl_demo
./jsonl_demo
```

## Tests

See `tests/src/unit-jsonl_stream.cpp` for the unit tests.
