// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The JSON of the automation protocol's frames: a strict reader (RFC 8259,
// UTF-8, nested at most max_json_depth deep) that turns a frame's JSON part
// into a tree of values, and a writer that builds one compactly, always in
// UTF-8: a text that is not UTF-8 is written as non_unicode_text_marker and
// the base64 of its bytes (unicode_text). Numbers are kept as written; the
// protocol's numbers are integers unless a field says otherwise.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::automation {

/// The deepest nesting of arrays and objects a JSON text may hold.
inline constexpr size_t max_json_depth = 64;

/// The kinds of JSON value.
enum class JsonType : uint8_t { null, boolean, number, string, array, object };

/// One JSON value of a parsed text, with everything inside it.
class Json {
  public:

    /// Returns the value's kind.
    ///
    /// @return the kind
    [[nodiscard]] JsonType type() const noexcept { return type_; }

    /// Returns a boolean's value.
    ///
    /// @return the value, or nothing for another kind of value
    [[nodiscard]] std::optional<bool> boolean() const noexcept;

    /// Returns a number that is a whole number within 64 bits.
    ///
    /// @return the number, or nothing for a fraction, an exponent, a number
    ///         beyond 64 bits or another kind of value
    [[nodiscard]] std::optional<int64_t> integer() const noexcept;

    /// Returns a string's text, its escapes replaced by the characters they stand for.
    ///
    /// @return the UTF-8 text, or null for another kind of value
    [[nodiscard]] const std::string* string() const noexcept;

    /// Returns a number's text as written.
    ///
    /// @return the text, or null for another kind of value
    [[nodiscard]] const std::string* number_text() const noexcept;

    /// Returns an array's elements.
    ///
    /// @return the elements in order; empty for another kind of value
    [[nodiscard]] std::span<const Json> elements() const noexcept;

    /// Returns an object's member names, in the order written.
    ///
    /// @return the names; empty for another kind of value
    [[nodiscard]] std::span<const std::string> names() const noexcept;

    /// Returns an object's member values, in the order of names().
    ///
    /// @return the values; empty for another kind of value
    [[nodiscard]] std::span<const Json> values() const noexcept;

    /// Finds an object's member by its name; the first of that name when several have it.
    ///
    /// @param name the member's name
    /// @return the member's value, or null when the object has none or this
    ///         is not an object
    [[nodiscard]] const Json* find(std::string_view name) const noexcept;

  private:

    friend struct JsonBuilder;

    JsonType type_ = JsonType::null;
    bool boolean_{};
    std::string text_;               ///< a string's text or a number's as written
    std::vector<Json> elements_;     ///< an array's elements, or an object's member values
    std::vector<std::string> names_; ///< an object's member names
};

/// Why a text is not JSON.
struct JsonError {
    std::string message; ///< what was wrong
    size_t offset{};     ///< the byte of the text where it was found
};

/// Parses a JSON text holding one value.
///
/// @param text the text, UTF-8 without a byte-order mark
/// @param[out] error why the text was refused; untouched on success
/// @return the value, or nothing when the text is not one JSON value
[[nodiscard]] std::optional<Json> parse_json(std::string_view text, JsonError& error);

/// The text that starts the string written in place of a text that is not
/// UTF-8; the standard base64 of the text's bytes follows it.
inline constexpr std::string_view non_unicode_text_marker = "Non-Unicode Text Error::";

/// Tells whether a text is UTF-8: no stray, overlong or cut-short sequence,
/// no surrogate and nothing above U+10FFFF.
///
/// @param text the text
/// @return true when it is
[[nodiscard]] bool utf8_valid(std::string_view text) noexcept;

/// Returns the standard base64 of some bytes (RFC 4648, its first alphabet,
/// padded with '=' to a multiple of four characters).
///
/// @param bytes the bytes
/// @return the base64 text
[[nodiscard]] std::string base64_text(std::span<const uint8_t> bytes);

/// Returns a text as the protocol's JSON carries it: as it is when it is
/// UTF-8; otherwise, whole, non_unicode_text_marker followed by the base64
/// of its bytes (base64_text), from which a client recovers them. The game's
/// own text in a code page that is not UTF-8 (captions, map, game and
/// player names, typed fields, chat, preferences) comes out so.
///
/// @param text the text, whatever its encoding
/// @return the text, UTF-8
[[nodiscard]] std::string unicode_text(std::string_view text);

/// Appends a text as a JSON string, quoted, with '"', '\\' and the control
/// characters escaped and every other byte kept as it is; a text that is
/// not UTF-8 is written as unicode_text gives it, so that the JSON is always
/// UTF-8.
///
/// @param[in,out] out the JSON being written
/// @param text the text, whatever its encoding
void append_json_string(std::string& out, std::string_view text);

/// Writes JSON compactly, value by value, separating them as it goes.
class JsonWriter {
  public:

    /// Starts an object.
    void begin_object();

    /// Ends the object last started.
    void end_object();

    /// Starts an array.
    void begin_array();

    /// Ends the array last started.
    void end_array();

    /// Writes a member's name; the value written next is the member's.
    ///
    /// @param name the name, written as append_json_string writes it
    void key(std::string_view name);

    /// Writes a string.
    ///
    /// @param text the text, written as append_json_string writes it
    void string(std::string_view text);

    /// Writes a whole number.
    ///
    /// @param value the number
    void integer(int64_t value);

    /// Writes a boolean.
    ///
    /// @param value the value
    void boolean(bool value);

    /// Writes null.
    void null();

    /// Writes a value given as JSON text, as it is.
    ///
    /// @param json one complete JSON value
    void raw(std::string_view json);

    /// Returns what has been written.
    ///
    /// @return the JSON text so far
    [[nodiscard]] const std::string& text() const noexcept { return out_; }

  private:

    /// Writes the separator a value needs before it.
    void before_value();

    std::string out_;
    std::vector<bool> has_items_; ///< for each open container, whether it holds a value yet
    bool after_key_{};            ///< a member's name was just written
};

} // namespace oa::app::automation
