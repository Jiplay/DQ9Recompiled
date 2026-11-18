// dq9_text.h — the game's text bytes as UTF-8.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace dq9 {

inline std::string CodePointToUtf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

// The game's text is ASCII, with other characters as tags: <:a> (umlaut),
// <'e> (acute), <`a> (grave), <^o> (circumflex), <~n> (tilde), <,c>
// (cedilla), <ss> (sharp s), <hos> (star)..., as in res/lang and the
// Greeting keyboard's layout (data/bin/keyboard_pr.bin). The renderer looks
// glyphs up by these byte sequences (0x020424F4). A tag becomes its UTF-8
// character; a tag or byte not known becomes '?'.
inline uint32_t TagToCodePoint(const std::string& tag) {
    struct Named {
        const char* tag;
        uint32_t cp;
    };
    static const Named kNamed[] = {
        {"ss", 0xDF},     {"ae", 0xE6},     {"AE", 0xC6},
        {"oe", 0x153},    {"OE", 0x152},    {"1", '\''},
        {"11", '"'},      {",", ','},       {"-", '-'},
        {"/", '/'},       {"]", '['},       {"_%_", '%'},
        {"wave", '~'},    {"^!", 0xA1},     {"^?", 0xBF},
        {"fem", 0xAA},    {"deg", 0xB0},    {"C=", 0x20AC},
        {"c/", 0xA2},     {"po", 0xA3},     {"<<", 0xAB},
        {">>", 0xBB},     {",,", 0x201E},   {"hos", 0x2605},
        {"l_a", 0x2190},  {"u_a", 0x2191},  {"r_a", 0x2192},
        {"d_a", 0x2193},  {"nknk", 0x263A},
    };
    for (const Named& n : kNamed)
        if (tag == n.tag) return n.cp;
    if (tag.size() != 2) return '?';
    static const char kMarks[] = ":'`^~,";
    // Latin-1 code points of each mark over a e i o u n c, upper then lower
    // case (0 = none).
    static const uint16_t kTable[6][14] = {
        {0xC4, 0xCB, 0xCF, 0xD6, 0xDC, 0, 0, 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0, 0},
        {0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0, 0, 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0, 0},
        {0xC0, 0xC8, 0xCC, 0xD2, 0xD9, 0, 0, 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0, 0},
        {0xC2, 0xCA, 0xCE, 0xD4, 0xDB, 0, 0, 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0, 0},
        {0, 0, 0, 0xD5, 0, 0xD1, 0, 0, 0, 0, 0xF5, 0, 0xF1, 0},
        {0, 0, 0, 0, 0, 0, 0xC7, 0, 0, 0, 0, 0, 0, 0xE7},
    };
    static const char kLetters[] = "AEIOUNCaeiounc";
    const char* mark = std::strchr(kMarks, tag[0]);
    const char* letter = std::strchr(kLetters, tag[1]);
    if (!mark || !letter || !*mark || !*letter) return '?';
    const uint16_t cp = kTable[mark - kMarks][letter - kLetters];
    return cp ? cp : '?';
}

inline std::string TagToUtf8(const std::string& tag) {
    return CodePointToUtf8(TagToCodePoint(tag));
}

// Up to `size` bytes or the first zero. A tag is '<', 1 to 4 bytes, '>':
// the first '>' after at least one byte ends it, so <<<> is a tag; <>>> is
// matched on its own.
inline std::string DecodeText(const uint8_t* bytes, size_t size) {
    std::string out;
    for (size_t i = 0; i < size && bytes[i]; ++i) {
        const uint8_t c = bytes[i];
        if (c == '<' && i + 3 < size && std::memcmp(bytes + i + 1, ">>>", 3) == 0) {
            out += TagToUtf8(">>");
            i += 3;
            continue;
        }
        if (c == '<') {
            size_t end = i + 2;
            while (end < size && bytes[end] && bytes[end] != '>' && end - i <= 4)
                ++end;
            if (end < size && bytes[end] == '>') {
                out += TagToUtf8(std::string(
                    reinterpret_cast<const char*>(bytes + i + 1), end - i - 1));
                i = end;
                continue;
            }
        }
        out += c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
    }
    return out;
}

}  // namespace dq9
