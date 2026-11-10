// Text the hooks add to the game, from res/lang/*.toml (embedded by
// scripts/gen-lang.py into lang.generated.h).
#pragma once

#include <cstdint>
#include <cstring>

#include "lang.generated.h"

namespace dq9 {

// The language DQ9 last loaded text in (the game context, 0x0200F398, +5;
// DS firmware codes, 1 English .. 5 Spanish). See language.cpp.
constexpr uint32_t kGameTextLanguage = 0x020F33D8u + 5u;

// The text for [section] key in DS language `language`, English when that
// language lacks it or is out of range, "" when English lacks it too.
inline const char* Tr(const char* section, const char* key, uint32_t language) {
    for (const LangText& t : kLangTexts) {
        if (std::strcmp(t.section, section) != 0 || std::strcmp(t.key, key) != 0)
            continue;
        if (language >= 1u && language <= 5u && t.text[language - 1u])
            return t.text[language - 1u];
        return t.text[0] ? t.text[0] : "";
    }
    return "";
}

}  // namespace dq9
