// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation protocol's JSON reader and writer (json.hpp).
#include "oa/app/automation/json.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <utility>

namespace oa::app::automation {

// Builds the values of a parsed text: the one place that writes Json's members.
struct JsonBuilder {
    /// Makes a value of a kind, with its text.
    ///
    /// @param type the kind
    /// @param text a string's text or a number's as written; empty otherwise
    /// @return the value
    static Json make(JsonType type, std::string text = {}) {
        Json value;
        value.type_ = type;
        value.text_ = std::move(text);
        return value;
    }

    /// Makes a boolean.
    ///
    /// @param flag the value
    /// @return the value
    static Json make_boolean(bool flag) {
        Json value = make(JsonType::boolean);
        value.boolean_ = flag;
        return value;
    }

    /// Adds an element to an array.
    ///
    /// @param[in,out] array the array
    /// @param element the element
    static void add_element(Json& array, Json element) {
        array.elements_.push_back(std::move(element));
    }

    /// Adds a member to an object.
    ///
    /// @param[in,out] object the object
    /// @param name the member's name
    /// @param member the member's value
    static void add_member(Json& object, std::string name, Json member) {
        object.names_.push_back(std::move(name));
        object.elements_.push_back(std::move(member));
    }
};

namespace {

// The code point a \u escape that is not a whole character stands for.
constexpr uint32_t replacement_character = 0xFFFD;
constexpr uint32_t first_high_surrogate = 0xD800;
constexpr uint32_t last_high_surrogate = 0xDBFF;
constexpr uint32_t first_low_surrogate = 0xDC00;
constexpr uint32_t last_low_surrogate = 0xDFFF;
constexpr uint32_t first_supplementary = 0x10000;
constexpr uint32_t last_code_point = 0x10FFFF;
// Hexadecimal digits of a \u escape.
constexpr size_t escape_digits = 4;

/// Appends a code point as UTF-8.
///
/// @param[in,out] out the text
/// @param code_point the code point, at most last_code_point
void append_utf8(std::string& out, uint32_t code_point) {
    if (code_point < 0x80) {
        out += static_cast<char>(code_point);
    } else if (code_point < 0x800) {
        out += static_cast<char>(0xC0 | (code_point >> 6));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point < first_supplementary) {
        out += static_cast<char>(0xE0 | (code_point >> 12));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code_point >> 18));
        out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    }
}

/// Measures the UTF-8 character that starts a text: a well-formed one, no
/// longer than it needs to be, not a surrogate and at most last_code_point.
///
/// @param text the text, from the character's first byte
/// @return its length in bytes, or 0 when it is not well formed
size_t utf8_length(std::string_view text) {
    const auto first = static_cast<uint8_t>(text[0]);
    size_t length = 0;
    uint32_t code_point = 0;
    uint32_t smallest = 0;
    if (first < 0x80)
        return 1;
    if ((first & 0xE0) == 0xC0) {
        length = 2;
        code_point = first & 0x1FU;
        smallest = 0x80;
    } else if ((first & 0xF0) == 0xE0) {
        length = 3;
        code_point = first & 0x0FU;
        smallest = 0x800;
    } else if ((first & 0xF8) == 0xF0) {
        length = 4;
        code_point = first & 0x07U;
        smallest = first_supplementary;
    } else {
        return 0;
    }
    if (text.size() < length)
        return 0;
    for (size_t at = 1; at < length; ++at) {
        const auto byte = static_cast<uint8_t>(text[at]);
        if ((byte & 0xC0) != 0x80)
            return 0;
        code_point = code_point << 6 | (byte & 0x3FU);
    }
    if (code_point < smallest || code_point > last_code_point ||
        (code_point >= first_high_surrogate && code_point <= last_low_surrogate))
        return 0;
    return length;
}

/// Reads the four hexadecimal digits of a \u escape.
///
/// @param text the digits
/// @param[out] value their value; unchanged on failure
/// @return false when they are not four hexadecimal digits
bool read_escape_digits(std::string_view text, uint32_t& value) {
    if (text.size() < escape_digits)
        return false;
    uint32_t result = 0;
    for (size_t at = 0; at < escape_digits; ++at) {
        const char c = text[at];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint32_t>(c - 'A' + 10);
        else
            return false;
        result = result << 4 | digit;
    }
    value = result;
    return true;
}

/// Tells whether a character is a decimal digit.
///
/// @param c the character
/// @return true for 0 to 9
bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Reads one JSON value from a text, recursively.
class Parser {
  public:

    /// Starts reading a text.
    ///
    /// @param text the text
    /// @param[out] error where a refusal is described
    Parser(std::string_view text, JsonError& error) : text_(text), error_(error) {}

    /// Reads the text's one value.
    ///
    /// @return the value, or nothing when the text is not one JSON value
    std::optional<Json> read_document() {
        Json value;
        skip_space();
        if (!read_value(value, 0))
            return std::nullopt;
        skip_space();
        if (at_ != text_.size()) {
            fail("text after the value");
            return std::nullopt;
        }
        return value;
    }

  private:

    /// Records why the text was refused, where reading stands.
    ///
    /// @param message what was wrong
    void fail(const char* message) {
        error_.message = message;
        error_.offset = at_;
    }

    /// Skips white space: spaces, tabs, line feeds and carriage returns.
    void skip_space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }

    /// Reads one value of any kind.
    ///
    /// @param[out] value the value
    /// @param depth the arrays and objects it is inside
    /// @return false when it is not a value
    bool read_value(Json& value, size_t depth) {
        if (at_ >= text_.size()) {
            fail("the text ends where a value should be");
            return false;
        }
        switch (text_[at_]) {
        case '{':
            return read_object(value, depth + 1);
        case '[':
            return read_array(value, depth + 1);
        case '"': {
            std::string text;
            if (!read_string(text))
                return false;
            value = JsonBuilder::make(JsonType::string, std::move(text));
            return true;
        }
        case 't':
            return read_word("true", JsonBuilder::make_boolean(true), value);
        case 'f':
            return read_word("false", JsonBuilder::make_boolean(false), value);
        case 'n':
            return read_word("null", JsonBuilder::make(JsonType::null), value);
        default:
            if (text_[at_] == '-' || is_digit(text_[at_]))
                return read_number(value);
            fail("an unexpected character");
            return false;
        }
    }

    /// Reads one of the words true, false and null.
    ///
    /// @param word the word
    /// @param made the value it stands for
    /// @param[out] value receives `made`
    /// @return false when the text does not hold the word
    bool read_word(std::string_view word, Json made, Json& value) {
        if (text_.substr(at_, word.size()) != word) {
            fail("an unexpected character");
            return false;
        }
        at_ += word.size();
        value = std::move(made);
        return true;
    }

    /// Reads a number: an optional minus, an integer part without leading
    /// zeros, an optional fraction and an optional exponent.
    ///
    /// @param[out] value the number, as written
    /// @return false when it is not a number
    bool read_number(Json& value) {
        const size_t start = at_;
        if (text_[at_] == '-')
            ++at_;
        if (at_ >= text_.size() || !is_digit(text_[at_])) {
            fail("a malformed number");
            return false;
        }
        if (text_[at_] == '0')
            ++at_;
        else
            while (at_ < text_.size() && is_digit(text_[at_]))
                ++at_;
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            if (at_ >= text_.size() || !is_digit(text_[at_])) {
                fail("a malformed fraction");
                return false;
            }
            while (at_ < text_.size() && is_digit(text_[at_]))
                ++at_;
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-'))
                ++at_;
            if (at_ >= text_.size() || !is_digit(text_[at_])) {
                fail("a malformed exponent");
                return false;
            }
            while (at_ < text_.size() && is_digit(text_[at_]))
                ++at_;
        }
        value = JsonBuilder::make(JsonType::number, std::string(text_.substr(start, at_ - start)));
        return true;
    }

    /// Reads a string, from its opening quote, replacing its escapes.
    ///
    /// A \u escape of half a surrogate pair that is not followed by the
    /// other half stands for U+FFFD.
    ///
    /// @param[out] out the string's text, UTF-8
    /// @return false when it is not a string
    bool read_string(std::string& out) {
        ++at_;
        while (at_ < text_.size()) {
            const auto c = static_cast<uint8_t>(text_[at_]);
            if (c == '"') {
                ++at_;
                return true;
            }
            if (c < 0x20) {
                fail("a control character in a string");
                return false;
            }
            if (c != '\\') {
                const size_t length = utf8_length(text_.substr(at_));
                if (length == 0) {
                    fail("a string that is not UTF-8");
                    return false;
                }
                out += text_.substr(at_, length);
                at_ += length;
                continue;
            }
            if (at_ + 1 >= text_.size()) {
                fail("an unfinished escape");
                return false;
            }
            const char escape = text_[at_ + 1];
            at_ += 2;
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                out += escape;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
                if (!read_unicode_escape(out))
                    return false;
                break;
            default:
                at_ -= 2;
                fail("an unknown escape");
                return false;
            }
        }
        fail("an unterminated string");
        return false;
    }

    /// Reads the digits of a \u escape, and the low half that follows a
    /// high surrogate, and appends the character they stand for.
    ///
    /// @param[in,out] out the string's text so far
    /// @return false when the digits are malformed
    bool read_unicode_escape(std::string& out) {
        uint32_t code_point = 0;
        if (!read_escape_digits(text_.substr(at_), code_point)) {
            fail("a malformed \\u escape");
            return false;
        }
        at_ += escape_digits;
        if (code_point >= first_high_surrogate && code_point <= last_high_surrogate) {
            uint32_t low = 0;
            const auto next = text_.substr(at_);
            if (next.size() >= 2 + escape_digits && next[0] == '\\' && next[1] == 'u' &&
                read_escape_digits(next.substr(2), low) && low >= first_low_surrogate &&
                low <= last_low_surrogate) {
                code_point = first_supplementary + ((code_point - first_high_surrogate) << 10) +
                             (low - first_low_surrogate);
                at_ += 2 + escape_digits;
            } else {
                code_point = replacement_character;
            }
        } else if (code_point >= first_low_surrogate && code_point <= last_low_surrogate) {
            code_point = replacement_character;
        }
        append_utf8(out, code_point);
        return true;
    }

    /// Reads an array, from its opening bracket.
    ///
    /// @param[out] value the array
    /// @param depth the arrays and objects it is inside, itself included
    /// @return false when it is not an array
    bool read_array(Json& value, size_t depth) {
        if (depth > max_json_depth) {
            fail("nested too deeply");
            return false;
        }
        ++at_;
        value = JsonBuilder::make(JsonType::array);
        skip_space();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            return true;
        }
        for (;;) {
            skip_space();
            Json element;
            if (!read_value(element, depth))
                return false;
            JsonBuilder::add_element(value, std::move(element));
            skip_space();
            if (at_ < text_.size() && text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
                return true;
            }
            fail("expected ',' or ']'");
            return false;
        }
    }

    /// Reads an object, from its opening brace.
    ///
    /// @param[out] value the object
    /// @param depth the arrays and objects it is inside, itself included
    /// @return false when it is not an object
    bool read_object(Json& value, size_t depth) {
        if (depth > max_json_depth) {
            fail("nested too deeply");
            return false;
        }
        ++at_;
        value = JsonBuilder::make(JsonType::object);
        skip_space();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            return true;
        }
        for (;;) {
            skip_space();
            if (at_ >= text_.size() || text_[at_] != '"') {
                fail("expected a member name");
                return false;
            }
            std::string name;
            if (!read_string(name))
                return false;
            skip_space();
            if (at_ >= text_.size() || text_[at_] != ':') {
                fail("expected ':'");
                return false;
            }
            ++at_;
            skip_space();
            Json member;
            if (!read_value(member, depth))
                return false;
            JsonBuilder::add_member(value, std::move(name), std::move(member));
            skip_space();
            if (at_ < text_.size() && text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
                return true;
            }
            fail("expected ',' or '}'");
            return false;
        }
    }

    std::string_view text_;
    JsonError& error_;
    size_t at_{}; ///< the byte being read
};

} // namespace

std::optional<bool> Json::boolean() const noexcept {
    if (type_ != JsonType::boolean)
        return std::nullopt;
    return boolean_;
}

std::optional<int64_t> Json::integer() const noexcept {
    if (type_ != JsonType::number || text_.find_first_of(".eE") != std::string::npos)
        return std::nullopt;
    int64_t value = 0;
    const auto [end, problem] = std::from_chars(text_.data(), text_.data() + text_.size(), value);
    if (problem != std::errc{} || end != text_.data() + text_.size())
        return std::nullopt;
    return value;
}

const std::string* Json::string() const noexcept {
    return type_ == JsonType::string ? &text_ : nullptr;
}

const std::string* Json::number_text() const noexcept {
    return type_ == JsonType::number ? &text_ : nullptr;
}

std::span<const Json> Json::elements() const noexcept {
    if (type_ != JsonType::array)
        return {};
    return elements_;
}

std::span<const std::string> Json::names() const noexcept {
    if (type_ != JsonType::object)
        return {};
    return names_;
}

std::span<const Json> Json::values() const noexcept {
    if (type_ != JsonType::object)
        return {};
    return elements_;
}

const Json* Json::find(std::string_view name) const noexcept {
    if (type_ != JsonType::object)
        return nullptr;
    for (size_t index = 0; index < names_.size(); ++index)
        if (names_[index] == name)
            return &elements_[index];
    return nullptr;
}

std::optional<Json> parse_json(std::string_view text, JsonError& error) {
    if (text.size() >= std::numeric_limits<uint32_t>::max()) {
        error = {"the text is too long", 0};
        return std::nullopt;
    }
    return Parser(text, error).read_document();
}

bool utf8_valid(std::string_view text) noexcept {
    for (size_t at = 0; at < text.size();) {
        const size_t length = utf8_length(text.substr(at));
        if (length == 0)
            return false;
        at += length;
    }
    return true;
}

std::string base64_text(std::span<const uint8_t> bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    // Three bytes make four characters of six bits each.
    constexpr size_t group_bytes = 3;
    constexpr uint32_t six_bits = 0x3F;
    std::string out;
    out.reserve((bytes.size() + group_bytes - 1) / group_bytes * 4);
    for (size_t at = 0; at < bytes.size(); at += group_bytes) {
        const size_t taken = std::min(group_bytes, bytes.size() - at);
        uint32_t group = uint32_t{bytes[at]} << 16;
        if (taken > 1)
            group |= uint32_t{bytes[at + 1]} << 8;
        if (taken > 2)
            group |= bytes[at + 2];
        out += alphabet[(group >> 18) & six_bits];
        out += alphabet[(group >> 12) & six_bits];
        out += taken > 1 ? alphabet[(group >> 6) & six_bits] : '=';
        out += taken > 2 ? alphabet[group & six_bits] : '=';
    }
    return out;
}

std::string unicode_text(std::string_view text) {
    if (utf8_valid(text))
        return std::string(text);
    std::string out(non_unicode_text_marker);
    out += base64_text({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    return out;
}

void append_json_string(std::string& out, std::string_view text) {
    static constexpr char hex[] = "0123456789abcdef";
    if (!utf8_valid(text)) {
        // What stands in for it is UTF-8, and needs no escape.
        append_json_string(out, unicode_text(text));
        return;
    }
    out += '"';
    for (const char c : text) {
        const auto byte = static_cast<uint8_t>(c);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte < 0x20) {
                out += "\\u00";
                out += hex[byte >> 4];
                out += hex[byte & 0xFU];
            } else {
                out += c;
            }
            break;
        }
    }
    out += '"';
}

void JsonWriter::before_value() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (!has_items_.empty()) {
        if (has_items_.back())
            out_ += ',';
        has_items_.back() = true;
    }
}

void JsonWriter::begin_object() {
    before_value();
    out_ += '{';
    has_items_.push_back(false);
}

void JsonWriter::end_object() {
    if (!has_items_.empty())
        has_items_.pop_back();
    out_ += '}';
}

void JsonWriter::begin_array() {
    before_value();
    out_ += '[';
    has_items_.push_back(false);
}

void JsonWriter::end_array() {
    if (!has_items_.empty())
        has_items_.pop_back();
    out_ += ']';
}

void JsonWriter::key(std::string_view name) {
    before_value();
    append_json_string(out_, name);
    out_ += ':';
    after_key_ = true;
}

void JsonWriter::string(std::string_view text) {
    before_value();
    append_json_string(out_, text);
}

void JsonWriter::integer(int64_t value) {
    before_value();
    out_ += std::to_string(value);
}

void JsonWriter::boolean(bool value) {
    before_value();
    out_ += value ? "true" : "false";
}

void JsonWriter::null() {
    before_value();
    out_ += "null";
}

void JsonWriter::raw(std::string_view json) {
    before_value();
    out_ += json;
}

} // namespace oa::app::automation
