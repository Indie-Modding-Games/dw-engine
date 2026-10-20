#include <dwe/formats/text/charset.hpp>
#include <dwe/formats/text/game_text.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <optional>
#include <string>

namespace dwe::formats::text {

namespace {

using enum ControlParameter;

constexpr std::uint8_t Raw03 = 0x03;
constexpr std::uint8_t NewLine = 0x0D;
constexpr std::uint8_t FirstGlyphByte = 0x20; // script cells starting below are control cells
constexpr std::uint8_t FirstLeadByte = 0x80;  // in byte strings, a byte from here starts a two-byte code
constexpr std::uint8_t Delete = 0x7F;

// The codes inside the battle's end-of-battle messages (battle_hud.c: BTL_drawBattleEndText): '#', then 'C' and the
// colour's character, 'R' (a new line) or 'W' (a new line after a wait).
constexpr char InlineCode = '#';
constexpr char InlineColor = 'C';
constexpr char InlineNewLine = 'R';
constexpr char InlineWait = 'W';
// A number the game prints into the string with sprintf (the fishing messages).
constexpr std::string_view PrintedNumber = "%d";
// The tags of the codes above. A printed number is its position among the string's numbers: {0}, {1}...
constexpr std::string_view ColorTag = "color";
constexpr std::string_view NewLineTag = "br";
constexpr std::string_view WaitTag = "wait";

// The inline code at `at`, as a tag, and the bytes it takes; nothing when the bytes there are not one.
struct InlineTag {
    std::string tag;
    std::size_t length = 0;
};

std::optional<InlineTag> inlineTagAt(std::span<const std::byte> raw, std::size_t at, int& numbers) {
    const auto charAt = [&raw](std::size_t index) {
        return index < raw.size() ? static_cast<char>(std::to_integer<std::uint8_t>(raw[index])) : '\0';
    };
    if (charAt(at) == InlineCode) {
        const char code = charAt(at + 1);
        if (code == InlineColor && std::isdigit(static_cast<unsigned char>(charAt(at + 2))) != 0) {
            return InlineTag{.tag = std::format("{{{}:{}}}", ColorTag, charAt(at + 2)), .length = 3};
        }
        if (code == InlineNewLine || code == InlineWait) {
            return InlineTag{.tag = std::format("{{{}}}", code == InlineNewLine ? NewLineTag : WaitTag), .length = 2};
        }
    }
    if (charAt(at) == PrintedNumber[0] && charAt(at + 1) == PrintedNumber[1]) {
        return InlineTag{.tag = std::format("{{{}}}", numbers++), .length = PrintedNumber.size()};
    }
    return std::nullopt;
}

constexpr std::array ControlTags = {
    ControlTag{.code = 0x01, .name = "color", .parameter = Value},
    ControlTag{.code = 0x02, .name = "raw02", .parameter = Raw},
    ControlTag{.code = 0x04, .name = "num", .parameter = Pstat},
    ControlTag{.code = 0x05, .name = "tamer", .parameter = Ignored},
    ControlTag{.code = 0x06, .name = "partner", .parameter = Ignored},
    ControlTag{.code = 0x07, .name = "digimon", .parameter = Pstat},
    ControlTag{.code = 0x08, .name = "move", .parameter = Pstat},
    ControlTag{.code = 0x09, .name = "item", .parameter = Pstat},
    ControlTag{.code = 0x0A, .name = "bits", .parameter = Ignored},
    ControlTag{.code = 0x0B, .name = "merit", .parameter = Ignored},
    ControlTag{.code = 0x0C, .name = "tab96", .parameter = Ignored},
    ControlTag{.code = 0x0E, .name = "tab132", .parameter = Ignored},
    ControlTag{.code = 0x0F, .name = "half", .parameter = Ignored},
    ControlTag{.code = 0x10, .name = "shopvar", .parameter = Ignored},
    ControlTag{.code = 0x11, .name = "bgm", .parameter = Pstat},
    ControlTag{.code = 0x12, .name = "tournament", .parameter = Pstat},
    ControlTag{.code = 0x13, .name = "tourn_won", .parameter = Ignored},
    ControlTag{.code = 0x14, .name = "tourn_wins", .parameter = Ignored},
    ControlTag{.code = 0x15, .name = "tourn_lost", .parameter = Ignored},
    ControlTag{.code = 0x16, .name = "col16", .parameter = Ignored},
    ControlTag{.code = 0x17, .name = "col17", .parameter = Ignored},
    ControlTag{.code = 0x18, .name = "col18", .parameter = Ignored},
    ControlTag{.code = 0x19, .name = "col19", .parameter = Ignored},
    ControlTag{.code = 0x1A, .name = "col1a", .parameter = Ignored},
    ControlTag{.code = 0x1B, .name = "col1b", .parameter = Ignored},
    ControlTag{.code = 0x1C, .name = "col1c", .parameter = Ignored},
};

std::uint8_t byteAt(std::span<const std::byte> bytes, std::size_t index) {
    return std::to_integer<std::uint8_t>(bytes[index]);
}

void appendCharacter(std::string& text, char32_t character) {
    if (character == '{') {
        text += "{{";
    } else if (character == '}') {
        text += "}}";
    } else {
        appendUtf8(text, character);
    }
}

std::optional<std::uint32_t> parseHex(std::string_view digits, std::size_t count) {
    std::uint32_t value = 0;
    if (digits.size() != count) {
        return std::nullopt;
    }
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint8_t> parseByte(std::string_view digits) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size() || value > 0xFF) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

// A piece of encoded text: a character, or the name and argument of a tag.
struct Token {
    std::optional<char32_t> character;
    std::string_view name;
    std::string_view argument;
};

// Splits text into characters and tags.
core::Result<std::vector<Token>> tokenize(std::string_view text) {
    std::vector<Token> tokens;
    while (!text.empty()) {
        if (text.starts_with("{{") || text.starts_with("}}")) {
            tokens.push_back(Token{.character = static_cast<char32_t>(text[0]), .name = {}, .argument = {}});
            text.remove_prefix(2);
            continue;
        }
        if (text[0] == '}') {
            return core::fail("a '}' that closes no tag; write a literal one as \"}}\"");
        }
        if (text[0] == '{') {
            const std::size_t close = text.find('}');
            if (close == std::string_view::npos) {
                return core::fail("a tag without its '}'");
            }
            const std::string_view tag = text.substr(1, close - 1);
            const std::size_t colon = tag.find(':');
            tokens.push_back(Token{.character = std::nullopt,
                                   .name = tag.substr(0, colon),
                                   .argument = colon == std::string_view::npos ? "" : tag.substr(colon + 1)});
            text.remove_prefix(close + 1);
            continue;
        }
        const auto character = takeUtf8(text);
        if (!character) {
            return core::fail("the text is not valid UTF-8");
        }
        tokens.push_back(Token{.character = character, .name = {}, .argument = {}});
    }
    return tokens;
}

core::Result<std::uint8_t> parseRawArgument(std::string_view argument, std::string_view tag) {
    if (argument.empty()) {
        return std::uint8_t{0};
    }
    const auto value = argument.starts_with("raw=") ? parseHex(argument.substr(4), 2) : std::nullopt;
    if (!value) {
        return core::fail(std::format("{{{}:{}}}: the argument must be raw=XX", tag, argument));
    }
    return static_cast<std::uint8_t>(*value);
}

// The parameter byte of a control tag from its argument.
core::Result<std::uint8_t> parseControlParameter(const ControlTag& tag, std::string_view argument) {
    const auto bad = [&](std::string_view expected) {
        return core::fail(std::format("{{{}:{}}}: the argument must be {}", tag.name, argument, expected));
    };
    switch (tag.parameter) {
    case Value:
        if (const auto value = parseByte(argument)) {
            return *value;
        }
        return bad("a number from 0 to 255");
    case Pstat:
        if (const auto value = argument.starts_with("pstat=") ? parseByte(argument.substr(6)) : std::nullopt) {
            return *value;
        }
        return bad("pstat=N with N from 0 to 255");
    case Raw:
        if (const auto value = parseHex(argument, 2)) {
            return static_cast<std::uint8_t>(*value);
        }
        return bad("two hex digits");
    case Ignored:
        return parseRawArgument(argument, tag.name);
    }
    return bad("valid");
}

void appendCell(std::vector<std::byte>& bytes, std::uint32_t first, std::uint32_t second) {
    bytes.push_back(static_cast<std::byte>(first));
    bytes.push_back(static_cast<std::byte>(second));
}

} // namespace

std::span<const ControlTag> scriptControlTags() {
    return ControlTags;
}

std::string decodeByteString(std::span<const std::byte> raw) {
    std::string text;
    int numbers = 0;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::uint8_t byte = byteAt(raw, i);
        if (auto tag = inlineTagAt(raw, i, numbers)) {
            text += tag->tag;
            i += tag->length - 1;
            continue;
        }
        if (byte >= FirstGlyphByte && byte < Delete) {
            appendCharacter(text, byte);
            continue;
        }
        if (byte < FirstLeadByte || i + 1 == raw.size()) {
            text += std::format("{{byte:{:02X}}}", byte);
            continue;
        }
        const auto code = static_cast<SjisCode>(byte << 8U | byteAt(raw, i + 1));
        if (const auto character = sjisToUnicode(code)) {
            appendCharacter(text, *character);
        } else {
            text += std::format("{{sjis:{:04X}}}", code);
        }
        ++i;
    }
    return text;
}

core::Result<std::vector<std::byte>> encodeByteString(std::string_view text) {
    const auto tokens = tokenize(text);
    if (!tokens) {
        return std::unexpected{tokens.error()};
    }
    std::vector<std::byte> bytes;
    const auto appendText = [&bytes](std::string_view text) {
        for (const char c : text) {
            bytes.push_back(static_cast<std::byte>(c));
        }
    };
    int numbers = 0;
    for (std::size_t i = 0; i < tokens->size(); ++i) {
        const Token& token = (*tokens)[i];
        if (token.character) {
            const char32_t character = *token.character;
            if (character >= FirstGlyphByte && character < Delete) {
                bytes.push_back(static_cast<std::byte>(character));
                continue;
            }
            const auto code = unicodeToSjis(character);
            if (!code) {
                return core::fail(std::format("U+{:04X} has no Shift-JIS code", static_cast<std::uint32_t>(character)));
            }
            appendCell(bytes, *code >> 8U, *code & 0xFFU);
            continue;
        }
        if (token.name == ColorTag) {
            if (token.argument.size() != 1 || std::isdigit(static_cast<unsigned char>(token.argument[0])) == 0) {
                return core::fail(
                    std::format("{{{}:{}}}: the colour of a byte string is one digit", ColorTag, token.argument));
            }
            appendText(std::string{InlineCode} + InlineColor + token.argument[0]);
        } else if (token.name == NewLineTag || token.name == WaitTag) {
            if (!token.argument.empty()) {
                return core::fail(std::format("{{{}}} takes no argument", token.name));
            }
            appendText(std::string{InlineCode} + (token.name == NewLineTag ? InlineNewLine : InlineWait));
        } else if (!token.name.empty() &&
                   std::ranges::all_of(token.name, [](char c) { return c >= '0' && c <= '9'; })) {
            if (token.name != std::to_string(numbers) || !token.argument.empty()) {
                return core::fail(
                    std::format("{{{}}}: the printed numbers of a byte string go in order from {{0}}", token.name));
            }
            ++numbers;
            appendText(PrintedNumber);
        } else if (token.name == "byte") {
            const auto value = parseHex(token.argument, 2);
            // A lead byte is alone only at the end; anywhere else it would pair with the next byte.
            if (!value || *value == 0 || (*value >= FirstLeadByte && i + 1 != tokens->size())) {
                return core::fail(std::format("{{byte:{}}} is not a byte this string can hold there", token.argument));
            }
            bytes.push_back(static_cast<std::byte>(*value));
        } else if (token.name == "sjis") {
            const auto value = parseHex(token.argument, 4);
            if (!value || (*value >> 8U) < FirstLeadByte || (*value & 0xFFU) == 0) {
                return core::fail(std::format("{{sjis:{}}} is not a two-byte code", token.argument));
            }
            appendCell(bytes, *value >> 8U, *value & 0xFFU);
        } else {
            return core::fail(std::format("{{{}}} is not a tag of byte strings", token.name));
        }
    }
    return bytes;
}

core::Result<ScriptText> decodeScriptText(std::span<const std::byte> bytes) {
    std::string text;
    std::size_t at = 0;
    while (true) {
        if (at + 2 > bytes.size()) {
            return core::fail("the text runs past the end of its bytes");
        }
        const std::uint8_t first = byteAt(bytes, at);
        const std::uint8_t second = byteAt(bytes, at + 1);
        if (first == Raw03) {
            if (at + 4 > bytes.size()) {
                return core::fail("a 0x03 cell runs past the end of the bytes");
            }
            text += std::format("{{raw03:{:02X}{:02X}{:02X}}}", second, byteAt(bytes, at + 2), byteAt(bytes, at + 3));
            at += 4;
            continue;
        }
        if (first == NewLine) {
            if (at + 2 < bytes.size() && byteAt(bytes, at + 2) == 0) {
                if (at + 4 > bytes.size()) {
                    return core::fail("the final cells run past the end of the bytes");
                }
                const std::uint8_t last = byteAt(bytes, at + 3);
                if (second != 0 || last != 0) {
                    text += std::format("{{end:{:02X}{:02X}}}", second, last);
                }
                return ScriptText{.text = std::move(text), .size = at + 4};
            }
            text += second == 0 ? std::string{"\n"} : std::format("{{newline:raw={:02X}}}", second);
            at += 2;
            continue;
        }
        if (first < FirstGlyphByte) {
            const auto tag = std::ranges::find(ControlTags, first, &ControlTag::code);
            if (tag == ControlTags.end()) {
                text += std::format("{{cell:{:02X}{:02X}}}", first, second);
            } else if (tag->parameter == Value) {
                text += std::format("{{{}:{}}}", tag->name, second);
            } else if (tag->parameter == Pstat) {
                text += std::format("{{{}:pstat={}}}", tag->name, second);
            } else if (tag->parameter == Raw) {
                text += std::format("{{{}:{:02X}}}", tag->name, second);
            } else if (second != 0) {
                text += std::format("{{{}:raw={:02X}}}", tag->name, second);
            } else {
                text += std::format("{{{}}}", tag->name);
            }
            at += 2;
            continue;
        }
        const auto code = static_cast<SjisCode>(first << 8U | second);
        if (const auto ascii = asciiOf(code)) {
            appendCharacter(text, static_cast<char32_t>(*ascii));
        } else if (const auto character = sjisToUnicode(code)) {
            appendCharacter(text, *character);
        } else {
            text += std::format("{{cell:{:04X}}}", code);
        }
        at += 2;
    }
}

core::Result<std::vector<std::byte>> encodeScriptText(std::string_view text) {
    const auto tokens = tokenize(text);
    if (!tokens) {
        return std::unexpected{tokens.error()};
    }
    std::vector<std::byte> bytes;
    std::uint32_t endParameter = 0;
    std::uint32_t endLast = 0;
    bool afterNewLine = false;
    for (std::size_t i = 0; i < tokens->size(); ++i) {
        const Token& token = (*tokens)[i];
        const std::size_t cellStart = bytes.size();
        if (token.character) {
            const char32_t character = *token.character;
            std::optional<SjisCode> code;
            if (character == '\n') {
                appendCell(bytes, NewLine, 0);
            } else if (character < 0x80) {
                code = fullWidthOf(static_cast<char>(character));
            } else {
                code = unicodeToSjis(character);
            }
            if (character != '\n') {
                if (!code) {
                    return core::fail(
                        std::format("U+{:04X} has no Shift-JIS code", static_cast<std::uint32_t>(character)));
                }
                appendCell(bytes, *code >> 8U, *code & 0xFFU);
            }
        } else if (token.name == "end") {
            const auto value = parseHex(token.argument, 4);
            if (!value || i + 1 != tokens->size()) {
                return core::fail("{end:XXYY} must be the last tag, with four hex digits");
            }
            endParameter = *value >> 8U;
            endLast = *value & 0xFFU;
        } else if (token.name == "newline") {
            const auto value = parseRawArgument(token.argument, token.name);
            if (!value) {
                return std::unexpected{value.error()};
            }
            appendCell(bytes, NewLine, *value);
        } else if (token.name == "raw03") {
            const auto value = parseHex(token.argument, 6);
            if (!value) {
                return core::fail("{raw03:XXXXXX} needs six hex digits");
            }
            appendCell(bytes, Raw03, *value >> 16U);
            appendCell(bytes, *value >> 8U & 0xFFU, *value & 0xFFU);
        } else if (token.name == "cell") {
            const auto value = parseHex(token.argument, 4);
            if (!value || (*value >> 8U) == Raw03 || (*value >> 8U) == NewLine) {
                return core::fail(std::format("{{cell:{}}} is not a two-byte cell", token.argument));
            }
            appendCell(bytes, *value >> 8U, *value & 0xFFU);
        } else {
            const auto tag = std::ranges::find(ControlTags, token.name, &ControlTag::name);
            if (tag == ControlTags.end()) {
                return core::fail(std::format("{{{}}} is not a tag of script text", token.name));
            }
            const auto parameter = parseControlParameter(*tag, token.argument);
            if (!parameter) {
                return std::unexpected{parameter.error()};
            }
            appendCell(bytes, tag->code, *parameter);
        }
        // A 0x00 byte right after a new line would end the text there.
        if (afterNewLine && bytes.size() > cellStart && bytes[cellStart] == std::byte{0}) {
            return core::fail("a cell starting with 0x00 cannot follow a new line");
        }
        if (bytes.size() > cellStart) {
            afterNewLine = bytes[cellStart] == std::byte{NewLine};
        }
    }
    appendCell(bytes, NewLine, endParameter);
    appendCell(bytes, 0, endLast);
    return bytes;
}

} // namespace dwe::formats::text
