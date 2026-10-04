//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013-2026 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef> // size_t
#include <string> // string
#include <utility> // move
#include <vector> // vector

#include <nlohmann/json.hpp>

NLOHMANN_JSON_NAMESPACE_BEGIN

/*!
@brief configuration for @ref jsonl_stream_decoder
*/
struct jsonl_decoder_config
{
    /*!
    @brief maximum number of bytes a single record may occupy

    The count is in **bytes**, not characters: it is the raw byte length of the
    record content (excluding the trailing line ending). A multi-byte UTF-8
    character therefore counts as 3 or 4 bytes, exactly as many as it occupies
    on the wire.

    Set to 0 to disable the limit (unlimited record size).
    */
    std::size_t max_record_bytes = 0;

    /*!
    @brief whether to skip blank lines

    A blank line is a line that contains only whitespace (spaces, tabs and
    carriage returns). When true (the default), blank lines are silently
    skipped and do not produce a record. When false, a blank line is parsed
    as JSON (which fails, since an empty string is not valid JSON).
    */
    bool skip_blank_lines = true;
};

/*!
@brief streaming decoder for JSON Lines (JSONL / newline-delimited JSON)

This class reads JSONL input in small byte chunks via @ref feed and delivers
one parsed JSON value per physical line. It reuses the existing @ref json
parser (@ref json::parse) for each complete record; it never reads the whole
input into memory at once.

A record is terminated by a physical line ending:

- LF
- CRLF
- a lone CR

An escaped newline inside a JSON string (the two-character sequence backslash
followed by the letter n) is **not** a line ending. Since that escape sequence
contains no raw newline byte, it is never mistaken for a record boundary.

Call @ref finish after the last chunk to parse a final record that has no
trailing line ending (this also covers a UTF-8 character split at EOF, which
the parser rejects as an error).

Once a record fails to parse or exceeds @ref jsonl_decoder_config::max_record_bytes,
the decoder enters a permanent error state: subsequent @ref feed calls return
0 and do not silently recover. Records delivered before the error remain
valid and accessible via @ref records.

@note This class is not thread-safe.
*/
class jsonl_stream_decoder
{
  public:
    /*!
    @brief construct a decoder

    @param[in] cfg  decoder configuration (see @ref jsonl_decoder_config)
    */
    explicit jsonl_stream_decoder(jsonl_decoder_config cfg = {})
        : m_cfg(std::move(cfg))
    {}

    /*!
    @brief feed a chunk of bytes

    Bytes are buffered internally until a complete physical record (a line
    terminated by LF, CRLF or a lone CR) is available. Each complete record is
    then parsed with the existing JSON parser and, on success, appended to
    @ref records.

    The decoder tracks a global byte offset across all @ref feed calls, so
    error positions are reported as absolute byte offsets from the start of
    the stream.

    @param[in] data  pointer to the chunk bytes
    @param[in] size  number of bytes in the chunk

    @return number of records delivered in this call (always 0 after an
            error or when @ref finish has been called)
    */
    std::size_t feed(const char* data, std::size_t size)
    {
        if (m_errored || m_finished || data == nullptr || size == 0)
        {
            return 0;
        }

        std::size_t delivered = 0;

        // append the whole chunk to the buffer; the buffer never holds more
        // than one record plus the incoming chunk, so memory use is bounded
        m_buffer.append(data, size);
        m_bytes_fed += size;

        // parse every complete record (line ending) in the buffer
        while (true)
        {
            std::size_t line_end = 0;
            const std::size_t line_start = find_line_ending(m_buffer, line_end);

            if (line_start == npos)
            {
                break; // no complete record yet
            }

            // the record content is [0, line_start); the line ending is
            // [line_start, line_end) and is not part of the JSON value
            const std::size_t record_bytes = line_start;

            if (m_cfg.max_record_bytes > 0 && record_bytes > m_cfg.max_record_bytes)
            {
                set_error("record exceeds maximum byte limit");
                return delivered;
            }

            if (parse_record(record_bytes))
            {
                ++delivered;
            }
            else
            {
                return delivered; // parse error: permanent error state
            }

            // remove the parsed record and its line ending from the buffer
            m_buffer.erase(0, line_end);
            m_record_start_offset = m_bytes_fed - m_buffer.size();
        }

        // if no complete line ending was found, the buffer holds an
        // incomplete record; check it against the byte limit
        if (m_cfg.max_record_bytes > 0 && m_buffer.size() > m_cfg.max_record_bytes)
        {
            set_error("record exceeds maximum byte limit");
            return delivered;
        }

        return delivered;
    }

    /*!
    @brief feed a chunk of bytes (std::string overload)

    @param[in] data  the chunk bytes

    @return number of records delivered in this call
    */
    std::size_t feed(const std::string& data)
    {
        return feed(data.data(), data.size());
    }

    /*!
    @brief signal end of stream

    Parses any buffered bytes as the final record. This is required when the
    last record has no trailing line ending. It also catches a UTF-8 character
    split at EOF: the parser rejects the incomplete sequence.

    After @ref finish, the decoder is closed: further @ref feed calls return
    0. Calling @ref finish more than once has no effect.

    @return number of records delivered in this call
    */
    std::size_t finish()
    {
        if (m_errored || m_finished)
        {
            return 0;
        }

        m_finished = true;

        if (m_buffer.empty())
        {
            return 0;
        }

        // a trailing CR is a lone CR line ending (the stream ended before
        // the matching LF arrived); strip it and parse what remains
        if (m_buffer.back() == '\r')
        {
            m_buffer.pop_back();
        }

        if (m_buffer.empty())
        {
            return 0;
        }

        if (m_cfg.max_record_bytes > 0 && m_buffer.size() > m_cfg.max_record_bytes)
        {
            set_error("record exceeds maximum byte limit");
            return 0;
        }

        if (parse_record(m_buffer.size()))
        {
            return 1;
        }

        return 0;
    }

    /// @return true if the decoder entered a permanent error state
    bool errored() const noexcept
    {
        return m_errored;
    }

    /// @return 1-based record number where the error occurred, or 0 if no error
    std::size_t error_record() const noexcept
    {
        return m_error_record;
    }

    /// @return absolute byte offset (from stream start) of the error record first byte
    std::size_t error_byte_offset() const noexcept
    {
        return m_error_byte_offset;
    }

    /// @return human-readable error message (empty if no error)
    const std::string& error_message() const noexcept
    {
        return m_error_message;
    }

    /// @return delivered JSON values, in order
    const std::vector<json>& records() const noexcept
    {
        return m_records;
    }

    /// @return number of records delivered so far
    std::size_t record_count() const noexcept
    {
        return m_records.size();
    }

    /// @return total number of bytes fed via @ref feed
    std::size_t bytes_fed() const noexcept
    {
        return m_bytes_fed;
    }

  private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    // Find the first physical line ending in @a buf.
    //
    // Returns the start position of the line ending and sets @a line_end to
    // the position just past it. Returns npos if no complete line ending is
    // found (the record is incomplete).
    //
    // Recognised line endings:
    //   LF      -> 1 byte
    //   CRLF    -> 2 bytes
    //   lone CR -> 1 byte (only when not at the buffer end, so a split CRLF
    //               across two feeds is kept for the next call)
    static std::size_t find_line_ending(const std::string& buf, std::size_t& line_end)
    {
        const std::size_t size = buf.size();

        // scan for LF
        for (std::size_t i = 0; i < size; ++i)
        {
            if (buf[i] == '\n')
            {
                // check for CRLF
                if (i > 0 && buf[i - 1] == '\r')
                {
                    line_end = i + 1;
                    return i - 1; // CRLF
                }
                line_end = i + 1;
                return i; // LF
            }
        }

        // no LF: scan for a lone CR (not at the last byte, which may be the
        // CR half of a CRLF split across two feeds)
        for (std::size_t i = 0; i + 1 < size; ++i)
        {
            if (buf[i] == '\r')
            {
                line_end = i + 1;
                return i; // lone CR
            }
        }

        return npos;
    }

    // Parse the record content [0, record_bytes) in m_buffer.
    // On success, appends the parsed value to m_records and returns true.
    // On failure, sets the permanent error state and returns false.
    bool parse_record(const std::size_t record_bytes)
    {
        // blank line handling
        if (m_cfg.skip_blank_lines && is_blank(m_buffer.data(), record_bytes))
        {
            return true; // skipped, not counted as a record
        }

        try
        {
            json j = json::parse(m_buffer.data(), m_buffer.data() + record_bytes);
            m_records.push_back(std::move(j));
            ++m_records_parsed; // count only successfully parsed records
            return true;
        }
        catch (const json::exception& e)
        {
            set_error(e.what());
            return false;
        }
    }

    void set_error(const std::string& msg)
    {
        m_errored = true;
        m_error_record = m_records_parsed + 1;
        m_error_byte_offset = m_record_start_offset;
        m_error_message = msg;
    }

    // Check whether [data, data + len) contains only whitespace.
    static bool is_blank(const char* data, const std::size_t len)
    {
        for (std::size_t i = 0; i < len; ++i)
        {
            switch (data[i])
            {
                case ' ':
                case '\t':
                case '\r':
                    continue;
                default:
                    return false;
            }
        }
        return true;
    }

    jsonl_decoder_config m_cfg;
    std::string m_buffer;
    std::vector<json> m_records;

    std::size_t m_bytes_fed = 0;
    std::size_t m_record_start_offset = 0;
    std::size_t m_records_parsed = 0;

    bool m_errored = false;
    bool m_finished = false;

    std::size_t m_error_record = 0;
    std::size_t m_error_byte_offset = 0;
    std::string m_error_message;
};

NLOHMANN_JSON_NAMESPACE_END
