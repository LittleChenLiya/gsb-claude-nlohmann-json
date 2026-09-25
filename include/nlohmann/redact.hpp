//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013 - 2025 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <process.h>
    #define NLOHMANN_JSON_REDACT_GETPID _getpid
#else
    #include <unistd.h>
    #define NLOHMANN_JSON_REDACT_GETPID getpid
#endif

#include <nlohmann/json.hpp>

// Streaming JSON redaction transcoder.
//
// This header is deliberately self-contained on top of the public API of
// <nlohmann/json.hpp>: it is not part of the amalgamated json.hpp and does
// not rely on any internal (detail) entity. It requires C++11 and C++
// exceptions.
//
// Resource complexity of nlohmann::json_redactor::transcribe:
//   - time:   O(n) in the number of input tokens (single SAX pass)
//   - memory: O(d + r) where d is the nesting depth of the input and r is
//             the total size of the configured rules; in particular the
//             memory footprint is independent of the document size
//   - disk:   O(m) for the staging sink, where m is the size of the
//             redacted output document; the output is never held in memory
//             as a whole, so arbitrarily large documents can be transcoded
//             and published atomically

NLOHMANN_JSON_NAMESPACE_BEGIN

/// @brief a single redaction rule: replace the exact JSON Pointer target
///        (and its whole subtree) with a fixed replacement value
struct redaction_rule
{
    redaction_rule(const json::json_pointer& pointer, json replacement_value)
        : target(pointer), replacement(std::move(replacement_value))
    {}

    redaction_rule(const std::string& pointer, json replacement_value)
        : target(pointer), replacement(std::move(replacement_value))
    {}

    json::json_pointer target;   ///< exact pointer; "" addresses the whole document
    json replacement;            ///< value written in place of the matched subtree
};

/// @brief staging output sink for the redaction transcoder
///
/// All transcribed bytes are written to the sink while parsing. Only when
/// the input document was consumed and validated completely, commit() is
/// called to publish the staged output atomically. On any failure (parse
/// error, I/O error, exception) abort() is called instead and the staged
/// output is discarded, so a half-written document is never published.
class redaction_sink
{
  public:
    virtual ~redaction_sink() = default;

    /// @brief append bytes to the staged output; throws on failure
    virtual void write(const char* data, std::size_t size) = 0;

    /// @brief publish the staged output atomically; throws on failure
    virtual void commit() = 0;

    /// @brief discard the staged output; must not throw
    virtual void abort() noexcept = 0;
};

/// @brief redaction_sink staging into a temporary file next to the target
///
/// Bytes are streamed to a temporary file in the same directory as the
/// target path. commit() flushes, closes, and atomically renames the
/// temporary file over the target (POSIX rename semantics; on Windows
/// MoveFileExA with MOVEFILE_REPLACE_EXISTING is used). abort() closes and
/// deletes the temporary file. The output document is never buffered in
/// memory as a whole.
class redaction_file_sink final : public redaction_sink
{
  public:
    explicit redaction_file_sink(std::string target_path)
        : target_path_(std::move(target_path))
        , temp_path_(make_temp_path(target_path_))
    {
        file_ = std::fopen(temp_path_.c_str(), "wb");
        if (file_ == nullptr)
        {
            throw std::runtime_error("redaction_file_sink: cannot open staging file " + temp_path_);
        }
    }

    redaction_file_sink(const redaction_file_sink&) = delete;
    redaction_file_sink& operator=(const redaction_file_sink&) = delete;

    ~redaction_file_sink() override
    {
        abort();
    }

    void write(const char* data, std::size_t size) override
    {
        if (file_ == nullptr || std::fwrite(data, 1, size, file_) != size)
        {
            throw std::runtime_error("redaction_file_sink: staging write failed for " + temp_path_);
        }
    }

    void commit() override
    {
        if (file_ != nullptr)
        {
            if (std::fflush(file_) != 0)
            {
                throw std::runtime_error("redaction_file_sink: staging flush failed for " + temp_path_);
            }
            std::fclose(file_);
            file_ = nullptr;
        }

#ifdef _WIN32
        if (MoveFileExA(temp_path_.c_str(), target_path_.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        {
            throw std::runtime_error("redaction_file_sink: cannot publish " + temp_path_ +
                                     " to " + target_path_);
        }
#else
        if (std::rename(temp_path_.c_str(), target_path_.c_str()) != 0)
        {
            throw std::runtime_error("redaction_file_sink: cannot publish " + temp_path_ +
                                     " to " + target_path_);
        }
#endif
        staged_ = false;
    }

    void abort() noexcept override
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
        if (staged_)
        {
            std::remove(temp_path_.c_str());
            staged_ = false;
        }
    }

    /// @brief path of the staging file (for diagnostics)
    const std::string& temp_path() const noexcept
    {
        return temp_path_;
    }

  private:
    static std::string make_temp_path(const std::string& target)
    {
        static unsigned long counter = 0;
        return target + ".tmp." + std::to_string(NLOHMANN_JSON_REDACT_GETPID()) +
               "." + std::to_string(counter++);
    }

    std::string target_path_;
    std::string temp_path_;
    std::FILE* file_ = nullptr;
    bool staged_ = true;
};

namespace detail
{

/// @brief SAX handler that echoes the input to a staging sink, replacing
///        exactly matched subtrees with their configured replacement
///
/// The handler never builds a DOM. While inside a replaced subtree it only
/// tracks the nesting depth, so the parser still validates the suppressed
/// input syntactically without invoking any output or user callback for it.
class redact_sax final : public json::json_sax_t
{
  public:
    using string_t = typename json::json_sax_t::string_t;
    using binary_t = typename json::json_sax_t::binary_t;

    redact_sax(const std::unordered_map<std::string, json>& rules, redaction_sink& sink)
        : rules_(rules), sink_(sink)
    {}

    bool null() override
    {
        return scalar("null", 4);
    }

    bool boolean(bool val) override
    {
        return val ? scalar("true", 4) : scalar("false", 5);
    }

    bool number_integer(number_integer_t val) override
    {
        const std::string token = std::to_string(val);
        return scalar(token.data(), token.size());
    }

    bool number_unsigned(number_unsigned_t val) override
    {
        const std::string token = std::to_string(val);
        return scalar(token.data(), token.size());
    }

    bool number_float(number_float_t /*val*/, const string_t& token) override
    {
        // Echo the raw token: the numeric value is preserved exactly as
        // parsed, with the precision of the parser's number_float_t.
        return scalar(token.data(), token.size());
    }

    bool string(string_t& val) override
    {
        if (suppressed_depth_ > 0)
        {
            return true;
        }
        emit_value_separator();
        if (const json* replacement = match())
        {
            emit(*replacement);
        }
        else
        {
            // re-serialize the string token with proper JSON escaping
            const std::string token = json(val).dump();
            sink_.write(token.data(), token.size());
        }
        advance_array_index();
        return true;
    }

    bool binary(binary_t& /*val*/) override
    {
        // never called for JSON input
        return true;
    }

    bool start_object(std::size_t /*elements*/) override
    {
        return start_container('{');
    }

    bool end_object() override
    {
        return end_container('}');
    }

    bool start_array(std::size_t /*elements*/) override
    {
        return start_container('[');
    }

    bool end_array() override
    {
        return end_container(']');
    }

    bool key(string_t& val) override
    {
        if (suppressed_depth_ == 0)
        {
            // object keys are structural: they are always echoed, together
            // with the entry separator and the name/value separator
            if (!frames_.empty() && !frames_.back().is_array)
            {
                if (frames_.back().entries > 0)
                {
                    sink_.write(",", 1);
                }
                ++frames_.back().entries;
            }
            const std::string token = json(val).dump() + ":";
            sink_.write(token.data(), token.size());
            current_key_ = val;
        }
        return true;
    }

    bool parse_error(std::size_t /*position*/, const std::string& /*last_token*/,
                     const json::exception& /*ex*/) override
    {
        // json::sax_parse reports the failure via its return value
        return false;
    }

  private:
    struct frame
    {
        bool is_array;
        std::size_t next_index;
        std::size_t entries;
    };

    bool scalar(const char* token, std::size_t size)
    {
        if (suppressed_depth_ > 0)
        {
            return true;
        }
        emit_value_separator();
        if (const json* replacement = match())
        {
            emit(*replacement);
        }
        else
        {
            sink_.write(token, size);
        }
        advance_array_index();
        return true;
    }

    bool start_container(char open)
    {
        if (suppressed_depth_ > 0)
        {
            ++suppressed_depth_;
            return true;
        }
        emit_value_separator();
        if (const json* replacement = match())
        {
            emit(*replacement);
            // suppress the matched subtree; it is still fully parsed and
            // validated, but neither echoed nor matched against child rules
            suppressed_depth_ = 1;
        }
        else
        {
            sink_.write(&open, 1);
            frames_.push_back(frame {open == '[', 0, 0});
        }
        return true;
    }

    bool end_container(char close)
    {
        if (suppressed_depth_ > 0)
        {
            if (--suppressed_depth_ == 0)
            {
                // the replaced subtree ended; it counts as one value
                advance_array_index();
            }
            return true;
        }
        sink_.write(&close, 1);
        frames_.pop_back();
        advance_array_index();
        return true;
    }

    /// @brief the replacement for the current value, or nullptr
    const json* match()
    {
        update_segment();
        json::json_pointer pointer;
        for (const auto& segment : segments_)
        {
            pointer /= segment;
        }
        const auto it = rules_.find(pointer.to_string());
        return it == rules_.end() ? nullptr : &it->second;
    }

    void update_segment()
    {
        segments_.resize(frames_.size());
        if (!frames_.empty())
        {
            const frame& top = frames_.back();
            segments_.back() = top.is_array
                               ? std::to_string(top.next_index)
                               : current_key_;
        }
    }

    void advance_array_index()
    {
        if (!frames_.empty() && frames_.back().is_array)
        {
            ++frames_.back().next_index;
        }
    }

    /// @brief emit the separator between two array elements, if needed
    ///        (object entry separators are emitted by key())
    void emit_value_separator()
    {
        if (!frames_.empty() && frames_.back().is_array && frames_.back().next_index > 0)
        {
            sink_.write(",", 1);
        }
    }

    void emit(const json& replacement)
    {
        const std::string token = replacement.dump();
        sink_.write(token.data(), token.size());
    }

    const std::unordered_map<std::string, json>& rules_;
    redaction_sink& sink_;
    std::vector<frame> frames_;
    std::vector<string_t> segments_;
    string_t current_key_;
    std::size_t suppressed_depth_ = 0;
};

} // namespace detail

/// @brief streaming redaction transcoder
///
/// Reads a JSON document with the SAX parser and writes a complete, valid
/// JSON document to a staging redaction_sink. Values addressed exactly by a
/// configured JSON Pointer rule are replaced by the rule's replacement
/// value; everything else is echoed preserving order and semantics.
///
/// Semantics:
///   - rules match exact JSON Pointers only (RFC 6901, including ~0/~1
///     escapes); the empty pointer "" addresses the whole document
///   - array indices refer to the positions in the original input
///   - once a parent path matched, no child rules are evaluated inside the
///     replaced subtree
///   - replaced subtrees are still fully parsed and syntactically
///     validated, but no DOM is built and no user callback is invoked
///   - numbers of unmatched values keep the precision of the parser's
///     number types (floating-point tokens are echoed verbatim)
///   - the staged output is only published (redaction_sink::commit) after
///     the complete input validated; on any failure nothing is published
class json_redactor
{
  public:
    json_redactor(std::vector<redaction_rule> rules, redaction_sink& sink)
        : sink_(&sink)
    {
        for (auto& rule : rules)
        {
            // later rules with the same pointer override earlier ones
            rules_[rule.target.to_string()] = std::move(rule.replacement);
        }
    }

    /// @brief transcribe JSON from @a input; returns true iff the input was
    ///        valid and the redacted document was published to the sink
    bool transcribe(std::istream& input)
    {
        detail::redact_sax sax(rules_, *sink_);
        bool parsed = false;
        try
        {
            parsed = json::sax_parse(input, &sax);
        }
        catch (...)
        {
            sink_->abort();
            throw;
        }
        if (!parsed)
        {
            sink_->abort();
            return false;
        }
        try
        {
            sink_->commit();
        }
        catch (...)
        {
            sink_->abort();
            throw;
        }
        return true;
    }

    /// @brief convenience overload for in-memory input
    bool transcribe(const std::string& input)
    {
        std::istringstream stream(input);
        return transcribe(stream);
    }

  private:
    std::unordered_map<std::string, json> rules_;
    redaction_sink* sink_;
};

NLOHMANN_JSON_NAMESPACE_END
