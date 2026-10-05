// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The readers of the files Steam and Heroic keep: Steam's text key-values
// (libraryfolders.vdf, appmanifest_<app>.acf) and Heroic's installed list
// (game_installs.hpp). Each reads the text it is given and nothing more;
// text that breaks off or breaks the format ends the reading, keeping what
// was read before.
#include "oa/platform/game_installs.hpp"

#include <charconv>

namespace oa::platform::game_installs {

namespace {

/// Converts UTF-8 text to a path, so that it is not read in a narrow code page.
///
/// @param text UTF-8 text
/// @return the path
std::filesystem::path path_of(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

/// Tells whether two ASCII words are equal, regardless of case.
///
/// @param left one word
/// @param right the other
/// @return true when they are the same letters
bool same_word(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;
    for (std::size_t at = 0; at < left.size(); ++at) {
        char a = left[at];
        char b = right[at];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Steam's text key-values

/// What a key-values token is.
enum class TokenKind : uint8_t {
    end,   ///< the text ended cleanly
    text,  ///< a string, quoted or bare
    open,  ///< {
    close, ///< }
    fault, ///< the text breaks the format (an unclosed string)
};

/// One token and its text.
struct Token {
    TokenKind kind{TokenKind::end}; ///< what it is
    std::string text{};             ///< a string's text, escapes undone
};

/// Reads Steam's text key-values a token at a time: quoted strings with backslash escapes,
/// bare words, braces, and // comments to the end of the line.
class KeyValuesReader {
  public:

    /// Starts reading a text.
    ///
    /// @param text the text
    explicit KeyValuesReader(std::string_view text) noexcept : text_(text) {}

    /// Reads the next token.
    ///
    /// @return the token; end at the end of the text, fault where the text breaks the format
    Token next() {
        skip_space();
        if (at_ >= text_.size())
            return {};
        const char first = text_[at_];
        if (first == '{') {
            ++at_;
            return {TokenKind::open, {}};
        }
        if (first == '}') {
            ++at_;
            return {TokenKind::close, {}};
        }
        if (first == '"')
            return quoted();
        return bare();
    }

  private:

    /// Moves past white space and comments.
    void skip_space() noexcept {
        while (at_ < text_.size()) {
            const char character = text_[at_];
            if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
                ++at_;
                continue;
            }
            if (character == '/' && at_ + 1 < text_.size() && text_[at_ + 1] == '/') {
                while (at_ < text_.size() && text_[at_] != '\n')
                    ++at_;
                continue;
            }
            return;
        }
    }

    /// Reads a quoted string; \\, \", \n and \t stand for their characters, and a backslash
    /// before anything else is kept with it.
    ///
    /// @return the string, or a fault when the text ends before its closing quote
    Token quoted() {
        ++at_;
        Token token{TokenKind::text, {}};
        while (at_ < text_.size()) {
            const char character = text_[at_++];
            if (character == '"')
                return token;
            if (character != '\\') {
                token.text += character;
                continue;
            }
            if (at_ >= text_.size())
                break;
            const char escaped = text_[at_++];
            switch (escaped) {
            case '\\':
                token.text += '\\';
                break;
            case '"':
                token.text += '"';
                break;
            case 'n':
                token.text += '\n';
                break;
            case 't':
                token.text += '\t';
                break;
            default:
                token.text += '\\';
                token.text += escaped;
                break;
            }
        }
        return {TokenKind::fault, {}};
    }

    /// Reads a bare word, up to white space, a quote or a brace.
    ///
    /// @return the word
    Token bare() {
        Token token{TokenKind::text, {}};
        while (at_ < text_.size()) {
            const char character = text_[at_];
            if (character == ' ' || character == '\t' || character == '\r' || character == '\n' ||
                character == '"' || character == '{' || character == '}')
                break;
            token.text += character;
            ++at_;
        }
        return token;
    }

    std::string_view text_{}; ///< the text
    std::size_t at_{};        ///< where reading stands
};

/// Walks a key-values text, calling `pair` with each key, its string value and the depth of
/// the block it lies in (0 at the top), until the text ends or breaks the format: a fault, a
/// block nested deeper than most_nesting, a close with no open, or a key with no value. The
/// visitor returns false to stop early.
///
/// @param text the text
/// @param pair called as pair(key, value, depth); returns whether to go on
template <typename Pair>
void walk_key_values(std::string_view text, Pair&& pair) {
    KeyValuesReader reader(text);
    std::size_t depth = 0;
    for (;;) {
        Token key = reader.next();
        if (key.kind == TokenKind::close) {
            if (depth == 0)
                return;
            --depth;
            continue;
        }
        if (key.kind != TokenKind::text)
            return;
        Token value = reader.next();
        if (value.kind == TokenKind::open) {
            if (++depth > most_nesting)
                return;
            continue;
        }
        if (value.kind != TokenKind::text)
            return;
        if (!pair(key.text, value.text, depth))
            return;
    }
}

/// Tells whether a key is a number: digits only.
///
/// @param key the key
/// @return true for one or more digits and nothing else
bool numbered(std::string_view key) noexcept {
    if (key.empty())
        return false;
    for (const char character : key)
        if (character < '0' || character > '9')
            return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Heroic's JSON

/// Reads the JSON of Heroic's installed list, taking the "install_path" of each object of the
/// top object's "installed" array and skipping everything else. Every value is read, so that
/// a fault anywhere ends the reading.
class InstalledListReader {
  public:

    /// Starts reading a text.
    ///
    /// @param text the text
    explicit InstalledListReader(std::string_view text) noexcept : text_(text) {}

    /// Reads the text.
    ///
    /// @return the install paths read before the end or the first fault
    std::vector<std::filesystem::path> read() {
        skip_space();
        if (!peek('{'))
            return std::move(paths_);
        ++at_;
        static_cast<void>(read_object_members(Place::top, 1));
        return std::move(paths_);
    }

  private:

    /// Where a value lies, which decides what is taken from it.
    enum class Place : uint8_t {
        elsewhere, ///< nothing is taken
        top,       ///< the top object
        installed, ///< the top object's "installed" array
        entry,     ///< an object in that array
    };

    /// Moves past white space.
    void skip_space() noexcept {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\r' || text_[at_] == '\n'))
            ++at_;
    }

    /// Tells whether the next character is one.
    ///
    /// @param character the character
    /// @return true when it is next
    [[nodiscard]] bool peek(char character) const noexcept {
        return at_ < text_.size() && text_[at_] == character;
    }

    /// Reads four hexadecimal digits.
    ///
    /// @param[out] value their value
    /// @return false when they are not four hexadecimal digits
    bool hex4(uint32_t& value) noexcept {
        if (text_.size() - at_ < 4)
            return false;
        value = 0;
        for (std::size_t digit = 0; digit < 4; ++digit) {
            const char character = text_[at_++];
            value <<= 4U;
            if (character >= '0' && character <= '9')
                value |= static_cast<uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f')
                value |= static_cast<uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F')
                value |= static_cast<uint32_t>(character - 'A' + 10);
            else
                return false;
        }
        return true;
    }

    /// Appends a character as UTF-8.
    ///
    /// @param[in,out] text the text
    /// @param code the character's number, at most 0x10ffff
    static void append_utf8(std::string& text, uint32_t code) {
        if (code < 0x80U) {
            text += static_cast<char>(code);
        } else if (code < 0x800U) {
            text += static_cast<char>(0xc0U | (code >> 6U));
            text += static_cast<char>(0x80U | (code & 0x3fU));
        } else if (code < 0x10000U) {
            text += static_cast<char>(0xe0U | (code >> 12U));
            text += static_cast<char>(0x80U | ((code >> 6U) & 0x3fU));
            text += static_cast<char>(0x80U | (code & 0x3fU));
        } else {
            text += static_cast<char>(0xf0U | (code >> 18U));
            text += static_cast<char>(0x80U | ((code >> 12U) & 0x3fU));
            text += static_cast<char>(0x80U | ((code >> 6U) & 0x3fU));
            text += static_cast<char>(0x80U | (code & 0x3fU));
        }
    }

    /// Reads a string, its escapes undone (\uXXXX, surrogate pairs included, as UTF-8).
    ///
    /// @param[out] text the string
    /// @return false at a fault: no opening quote, an unknown escape, a control character or no
    ///     closing quote
    bool read_string(std::string& text) {
        constexpr uint32_t high_surrogate_first = 0xd800U;
        constexpr uint32_t low_surrogate_first = 0xdc00U;
        constexpr uint32_t surrogate_end = 0xe000U;
        constexpr uint32_t supplementary_first = 0x10000U;
        constexpr uint32_t surrogate_bits = 10U;
        constexpr unsigned char least_printable = 0x20U;
        text.clear();
        if (!peek('"'))
            return false;
        ++at_;
        while (at_ < text_.size()) {
            const char character = text_[at_++];
            if (character == '"')
                return true;
            if (static_cast<unsigned char>(character) < least_printable)
                return false;
            if (character != '\\') {
                text += character;
                continue;
            }
            if (at_ >= text_.size())
                return false;
            switch (text_[at_++]) {
            case '"':
                text += '"';
                break;
            case '\\':
                text += '\\';
                break;
            case '/':
                text += '/';
                break;
            case 'b':
                text += '\b';
                break;
            case 'f':
                text += '\f';
                break;
            case 'n':
                text += '\n';
                break;
            case 'r':
                text += '\r';
                break;
            case 't':
                text += '\t';
                break;
            case 'u': {
                uint32_t code = 0;
                if (!hex4(code))
                    return false;
                if (code >= high_surrogate_first && code < low_surrogate_first) {
                    uint32_t low = 0;
                    if (text_.size() - at_ < 2 || text_[at_] != '\\' || text_[at_ + 1] != 'u')
                        return false;
                    at_ += 2;
                    if (!hex4(low) || low < low_surrogate_first || low >= surrogate_end)
                        return false;
                    code = supplementary_first + ((code - high_surrogate_first) << surrogate_bits) +
                           (low - low_surrogate_first);
                } else if (code >= low_surrogate_first && code < surrogate_end) {
                    return false;
                }
                append_utf8(text, code);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    /// Reads a number, true, false or null: the characters a number or a word may hold.
    ///
    /// @return false when there are none
    bool read_scalar() noexcept {
        const std::size_t start = at_;
        while (at_ < text_.size()) {
            const char character = text_[at_];
            const bool part = (character >= '0' && character <= '9') ||
                              (character >= 'a' && character <= 'z') || character == '-' ||
                              character == '+' || character == '.' || character == 'E';
            if (!part)
                break;
            ++at_;
        }
        return at_ > start;
    }

    /// Reads one value.
    ///
    /// @param place where it lies
    /// @param depth how deeply it is nested
    /// @param[out] text the value when it is a string
    /// @return false at a fault
    bool read_value(Place place, std::size_t depth, std::string* text) {
        skip_space();
        if (at_ >= text_.size())
            return false;
        if (peek('"')) {
            std::string value;
            if (!read_string(value))
                return false;
            if (text != nullptr)
                *text = std::move(value);
            return true;
        }
        if (peek('{') || peek('[')) {
            if (depth + 1 > most_nesting)
                return false;
            const bool object = peek('{');
            ++at_;
            return object ? read_object_members(place, depth + 1)
                          : read_array_elements(place, depth + 1);
        }
        return read_scalar();
    }

    /// Reads an object's members after its opening brace, up to and with its closing brace.
    ///
    /// @param place where the object lies
    /// @param depth how deeply it is nested
    /// @return false at a fault
    bool read_object_members(Place place, std::size_t depth) {
        skip_space();
        if (peek('}')) {
            ++at_;
            return true;
        }
        for (;;) {
            skip_space();
            std::string key;
            if (!read_string(key))
                return false;
            skip_space();
            if (!peek(':'))
                return false;
            ++at_;
            Place inner = Place::elsewhere;
            if (place == Place::top && key == "installed")
                inner = Place::installed;
            std::string value;
            const bool wanted = place == Place::entry && key == "install_path";
            if (!read_value(inner, depth, wanted ? &value : nullptr))
                return false;
            if (wanted && !value.empty() && paths_.size() < most_library_entries)
                paths_.push_back(path_of(value));
            skip_space();
            if (peek(',')) {
                ++at_;
                continue;
            }
            if (peek('}')) {
                ++at_;
                return true;
            }
            return false;
        }
    }

    /// Reads an array's elements after its opening bracket, up to and with its closing bracket.
    ///
    /// @param place where the array lies
    /// @param depth how deeply it is nested
    /// @return false at a fault
    bool read_array_elements(Place place, std::size_t depth) {
        skip_space();
        if (peek(']')) {
            ++at_;
            return true;
        }
        const Place inner = place == Place::installed ? Place::entry : Place::elsewhere;
        for (;;) {
            if (!read_value(inner, depth, nullptr))
                return false;
            skip_space();
            if (peek(',')) {
                ++at_;
                continue;
            }
            if (peek(']')) {
                ++at_;
                return true;
            }
            return false;
        }
    }

    std::string_view text_{};                    ///< the text
    std::size_t at_{};                           ///< where reading stands
    std::vector<std::filesystem::path> paths_{}; ///< the install paths read
};

} // namespace

std::vector<std::filesystem::path> library_folders(std::string_view text) {
    std::vector<std::filesystem::path> folders;
    walk_key_values(
        text, [&folders](std::string_view key, std::string_view value, std::size_t depth) {
            // Today's files give each library a block with a "path"; older ones named each
            // library by a number directly under the top block.
            if ((same_word(key, "path") || (depth == 1 && numbered(key))) && !value.empty())
                folders.push_back(path_of(value));
            return folders.size() < most_library_entries;
        }
    );
    return folders;
}

std::optional<std::string> manifest_install_dir(std::string_view text, uint32_t app) {
    std::optional<uint32_t> appid;
    std::optional<std::string> install_dir;
    walk_key_values(text, [&](std::string_view key, std::string_view value, std::size_t depth) {
        if (depth != 1)
            return true;
        if (!appid && same_word(key, "appid")) {
            uint32_t number = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), number);
            if (error == std::errc{} && end == value.data() + value.size())
                appid = number;
            else
                appid = 0;
        } else if (!install_dir && same_word(key, "installdir")) {
            install_dir = std::string(value);
        }
        return !appid || !install_dir;
    });
    if (!appid || *appid != app || !install_dir || install_dir->empty())
        return std::nullopt;
    return install_dir;
}

std::vector<std::filesystem::path> heroic_install_paths(std::string_view text) {
    return InstalledListReader(text).read();
}

} // namespace oa::platform::game_installs
