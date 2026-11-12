// A DQ9 save image from a save file in any of the usual wrappers (.duc
// Action Replay, .dsv, padded .sav...), for the title menu's "Import save
// file" row (options_row.cpp). The same conversion as
// scripts/convert_save.py: the save is region independent (same layout and
// charset for EU/US), so this only strips whatever precedes the
// "DRAGON QUEST IX" marker and keeps the 65536 bytes after it. Both slots'
// CRC32s are checked; a mismatch is reported and the image kept anyway.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace dq9::save {

constexpr char kMarker[] = "DRAGON QUEST IX";
constexpr size_t kMarkerSize = sizeof(kMarker) - 1;
constexpr size_t kSize = 65536;
constexpr size_t kSlot = 32768;

inline uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline uint32_t ReadU32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Logs each slot's CRCs; false when a slot with a marker fails them.
inline bool Check(const uint8_t* image) {
    bool ok = true;
    for (size_t i = 0; i < 2; ++i) {
        const uint8_t* s = image + i * kSlot;
        if (std::memcmp(s, kMarker, kMarkerSize) != 0) {
            std::fprintf(stderr, "[save] slot %zu: no marker (empty slot?)\n", i);
            continue;
        }
        const bool good = Crc32(s + 20, 16) == ReadU32(s + 16) &&
                          Crc32(s + 136, 28508) == ReadU32(s + 132);
        std::fprintf(stderr, "[save] slot %zu: CRCs %s\n", i,
                     good ? "ok" : "MISMATCH");
        ok &= good;
    }
    return ok;
}

inline bool Convert(const std::vector<uint8_t>& file, std::vector<uint8_t>* image,
                    std::string* error) {
    const auto mark = std::search(file.begin(), file.end(), kMarker,
                                  kMarker + kMarkerSize);
    if (mark == file.end()) {
        *error = "no DRAGON QUEST IX save in this file";
        return false;
    }
    const size_t pos = static_cast<size_t>(mark - file.begin());
    if (file.size() - pos < kSize) {
        *error = "only " + std::to_string(file.size() - pos) +
                 " bytes after the save's start, need " + std::to_string(kSize);
        return false;
    }
    image->assign(mark, mark + kSize);
    std::fprintf(stderr,
                 "[save] header %zu bytes stripped, %zu trailing bytes dropped\n",
                 pos, file.size() - pos - kSize);
    if (!Check(image->data()))
        std::fprintf(stderr, "[save] warning: checksum mismatch, importing anyway\n");
    return true;
}

}  // namespace dq9::save
