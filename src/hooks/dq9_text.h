// dq9_text.h — the game's text bytes as UTF-8.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace dq9 {

// The game's text is ASCII, with accented letters as tags: <:a> (umlaut),
// <'e> (acute), <`a> (grave), <^o> (circumflex), <~n> (tilde), <,c>
// (cedilla), <ss> (sharp s), as in res/lang. The renderer looks glyphs up by
// these byte sequences (0x020424F4). A tag becomes its UTF-8 letter; a tag
// or byte not known becomes '?'.
inline std::string TagToUtf8(const std::string& tag) {
    if (tag == "ss") return "\xC3\x9F";
    if (tag == "1") return "'";
    if (tag == ",") return ",";
    if (tag.size() != 2) return "?";
    static const char kMarks[] = ":'`^~,";
    // Latin-1 code points of each mark over a e i o u n c, upper then lower
    // case (0 = none).
    static const uint16_t kTable[6][14] = {
        {0xC4, 0xCB, 0xCF, 0xD6, 0xDC, 0, 0, 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0, 0},
        {0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0, 0, 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0, 0},
        {0xC0, 0xC8, 0xCC, 0xD2, 0xD9, 0, 0, 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0, 0},
        {0xC2, 0xCA, 0xCE, 0xD4, 0xDB, 0, 0, 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0, 0},
        {0, 0, 0, 0, 0, 0xD1, 0, 0, 0, 0, 0, 0, 0xF1, 0},
        {0, 0, 0, 0, 0, 0, 0xC7, 0, 0, 0, 0, 0, 0, 0xE7},
    };
    static const char kLetters[] = "AEIOUNCaeiounc";
    const char* mark = std::strchr(kMarks, tag[0]);
    const char* letter = std::strchr(kLetters, tag[1]);
    if (!mark || !letter || !*mark || !*letter) return "?";
    const uint16_t cp = kTable[mark - kMarks][letter - kLetters];
    if (!cp) return "?";
    return {static_cast<char>(0xC0 | (cp >> 6)),
            static_cast<char>(0x80 | (cp & 0x3F))};
}

// Up to `size` bytes or the first zero.
inline std::string DecodeText(const uint8_t* bytes, size_t size) {
    std::string out;
    for (size_t i = 0; i < size && bytes[i]; ++i) {
        const uint8_t c = bytes[i];
        if (c == '<') {
            size_t end = i + 1;
            while (end < size && bytes[end] && bytes[end] != '>' && end - i <= 3)
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
