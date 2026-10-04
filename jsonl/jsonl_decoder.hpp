// jsonl_decoder.hpp - incremental JSON Lines (JSONL / NDJSON) decoder.
//
// Feeds arbitrary small byte chunks, reuses nlohmann::json's parser per
// record, and never requires the whole input in memory. A record ends only
// at a physical LF byte (0x0A); escaped "\n" inside JSON strings is two
// bytes (backslash, 'n') and therefore never a record boundary.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace nlohmann
{
namespace jsonl
{

/// Error reported by the decoder. Carries the 1-based record number and the
/// global byte position (0-based offset within the whole byte stream) at
/// which the problem was detected:
///  - invalid JSON        -> offset of the first byte of the failing record
///  - record size limit   -> offset of the byte that exceeded the limit
///  - truncated UTF-8     -> offset of the incomplete lead byte
class error : public std::runtime_error
{
  public:
    error(std::size_t record, std::size_t byte_offset, const std::string& message)
        : std::runtime_error(message), record_(record), byte_offset_(byte_offset)
    {}

    /// 1-based number of the record that failed (blank lines do not count).
    std::size_t record() const noexcept { return record_; }
    /// Global 0-based byte offset within the fed stream.
    std::size_t byte_offset() const noexcept { return byte_offset_; }

  private:
    std::size_t record_;
    std::size_t byte_offset_;
};

/// Incremental JSONL decoder.
///
/// Usage:
///   nlohmann::jsonl::decoder dec(max_record_bytes);
///   for each chunk:  auto records = dec.feed(data, size);
///   at end of input: auto tail    = dec.finish();   // final record w/o LF
///
/// Semantics:
///  - Records are delimited by physical LF; a preceding CR is stripped (CRLF).
///  - Blank lines (empty or JSON-whitespace only) are skipped and do not
///    count as records.
///  - The per-record limit counts *bytes* (UTF-8 code units), not characters;
///    the LF delimiter is not counted, a CR of a CRLF pair is.
///  - finish() flushes a trailing record that has no terminating LF and
///    rejects input ending in the middle of a UTF-8 sequence.
///  - On bad JSON, limit overflow or truncated UTF-8 the decoder throws
///    jsonl::error and latches into a failed state: every later feed() or
///    finish() rethrows the same error - it cannot silently resume.
///  - Records already returned by earlier feed()/finish() calls are plain
///    values and stay valid regardless of later failures.
template<typename BasicJson = nlohmann::json>
class basic_decoder
{
  public:
    using json_type = BasicJson;

    static constexpr std::size_t unlimited = static_cast<std::size_t>(-1);
    static constexpr std::size_t default_max_record_bytes = 16u * 1024u * 1024u;

    explicit basic_decoder(std::size_t max_record_bytes = default_max_record_bytes)
        : max_record_bytes_(max_record_bytes)
    {}

    basic_decoder(const basic_decoder&) = delete;
    basic_decoder& operator=(const basic_decoder&) = delete;

    /// Feed one chunk of bytes. Returns the records completed by this chunk
    /// (possibly empty, possibly several). Throws jsonl::error on failure.
    std::vector<BasicJson> feed(const char* data, std::size_t size)
    {
        return feed(std::string_view(data, size));
    }

    std::vector<BasicJson> feed(std::string_view chunk)
    {
        throw_if_failed();
        if (finished_)
        {
            throw std::logic_error("jsonl::basic_decoder: feed() after finish()");
        }
        std::vector<BasicJson> ready;
        for (const char ch : chunk)
        {
            if (ch == '\n')
            {
                end_of_record(ready);
            }
            else
            {
                if (buffer_.size() >= max_record_bytes_)
                {
                    fail_at(position_,
                            "record " + std::to_string(delivered_ + 1) +
                            " exceeds the per-record limit of " +
                            std::to_string(max_record_bytes_) + " bytes");
                }
                buffer_.push_back(ch);
            }
            ++position_;
        }
        return ready;
    }

    /// Signal end of input. Flushes a final record without trailing LF.
    /// Throws jsonl::error if the pending bytes end mid-UTF-8-sequence or
    /// the final record is invalid JSON. Idempotent when successful.
    std::vector<BasicJson> finish()
    {
        throw_if_failed();
        if (finished_)
        {
            return {};
        }
        finished_ = true;
        std::vector<BasicJson> ready;
        const std::size_t tail = truncated_utf8_tail();
        if (tail != npos)
        {
            fail_at(record_start() + tail,
                    "record " + std::to_string(delivered_ + 1) +
                    " ends with a truncated UTF-8 sequence");
        }
        if (!buffer_.empty())
        {
            end_of_record(ready);
        }
        return ready;
    }

    /// True after an error was thrown; the decoder stays failed.
    bool failed() const noexcept { return failed_; }
    /// The latched error; only meaningful when failed() is true.
    const jsonl::error& last_error() const { return last_error_; }
    /// Number of records successfully delivered so far.
    std::size_t records_delivered() const noexcept { return delivered_; }
    /// Total number of input bytes consumed so far (global stream position).
    std::size_t bytes_consumed() const noexcept { return position_; }
    /// Configured per-record byte limit.
    std::size_t max_record_bytes() const noexcept { return max_record_bytes_; }

  private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    std::size_t record_start() const noexcept { return position_ - buffer_.size(); }

    static bool is_blank(const std::string& line) noexcept
    {
        for (const char c : line)
        {
            if (c != ' ' && c != '\t' && c != '\r')
            {
                return false;
            }
        }
        return true;
    }

    void end_of_record(std::vector<BasicJson>& ready)
    {
        const std::size_t start = record_start();
        std::string line = std::move(buffer_);
        buffer_.clear();
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();  // CRLF: strip the carriage return
        }
        if (is_blank(line))
        {
            return;  // blank-line strategy: skip silently, not a record
        }
        try
        {
            ready.push_back(BasicJson::parse(line));
        }
        catch (const typename BasicJson::parse_error& e)
        {
            fail_at(start,
                    "record " + std::to_string(delivered_ + 1) +
                    " is not valid JSON (record byte " + std::to_string(e.byte) +
                    "): " + e.what());
        }
        ++delivered_;
    }

    /// If the pending buffer ends with an incomplete UTF-8 sequence, returns
    /// the offset of its lead byte within the buffer; otherwise npos.
    /// Malformed (not merely truncated) sequences are left to the JSON parser.
    std::size_t truncated_utf8_tail() const noexcept
    {
        const std::size_t n = buffer_.size();
        std::size_t i = n;
        std::size_t continuations = 0;
        while (i > 0 && continuations < 3 &&
               (static_cast<unsigned char>(buffer_[i - 1]) & 0xC0u) == 0x80u)
        {
            --i;
            ++continuations;
        }
        if (i == 0)
        {
            return npos;  // only continuation bytes: malformed, parser handles it
        }
        const unsigned char lead = static_cast<unsigned char>(buffer_[i - 1]);
        std::size_t expected = 0;
        if (lead < 0x80u)                 { return npos; }
        else if ((lead & 0xE0u) == 0xC0u) { expected = 2; }
        else if ((lead & 0xF0u) == 0xE0u) { expected = 3; }
        else if ((lead & 0xF8u) == 0xF0u) { expected = 4; }
        else                              { return npos; }  // invalid lead byte
        const std::size_t have = n - (i - 1);
        return have < expected ? i - 1 : npos;
    }

    [[noreturn]] void fail_at(std::size_t offset, const std::string& message)
    {
        failed_ = true;
        last_error_ = jsonl::error(delivered_ + 1, offset, message);
        throw last_error_;
    }

    void throw_if_failed() const
    {
        if (failed_)
        {
            throw last_error_;  // latched: no silent resume
        }
    }

    std::size_t max_record_bytes_;
    std::string buffer_;
    std::size_t position_ = 0;    // global offset of the next byte to consume
    std::size_t delivered_ = 0;   // records delivered so far
    bool finished_ = false;
    bool failed_ = false;
    jsonl::error last_error_{0, 0, ""};
};

using decoder = basic_decoder<nlohmann::json>;

}  // namespace jsonl
}  // namespace nlohmann
