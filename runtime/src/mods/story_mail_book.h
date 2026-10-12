// Story-mod letters (docs/mod-sdk-v2.md, "Letters"): the record of the letters a save file has
// received, and the text a letter shows. Pure code without game memory, unit-tested in
// runtime/tools/story_mail_test.cpp.
//
// Where the record lives: the game's save data has a reserved block (dSv_reserve_c, 0x50 bytes at
// save base + 0x744, right after the event flags). dSv_info_c::memory_to_card / card_to_memory copy
// it with the save and dSv_reserve_c::init zeroes it for a new file (and for the Second Quest); no
// other decompiled code touches it, and it is zero in every save file checked (213 Quest Logs, up to
// late-game progress). So letter states are saved and loaded with the save file and restored by full
// and portable save states, exactly like the game's own mail flags.
//
// Layout (bytes, no alignment needs):
//   [0] 'L' (0x4C) magic, [1] format version 1, [2..3] zero;
//   then 19 records of 4 bytes: a 24-bit key (never 0) and a state byte.
// An all-zero block is an empty book. A block that is not zero and has no valid header holds data
// this port did not write: it is never changed.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace story_mail {

inline constexpr uint32_t kBookOffset = 0x744;  // from the save base (*0x101F84DC)
inline constexpr uint32_t kBookSize = 0x50;
inline constexpr uint8_t kMagic = 0x4C, kVersion = 1;
inline constexpr unsigned kRecords = (kBookSize - 4) / 4;  // 19

// Letter states. The values follow the game's own letter registers (dLetter: 2 waiting in the
// postbox, 3 read); 0 is "never sent".
enum State : uint8_t { kNone = 0, kWaiting = 2, kRead = 3 };

// FNV-1a over "mod id" NUL "letter id", folded to 24 bits; 0 is reserved for an empty record.
inline uint32_t letter_key(const std::string& mod, const std::string& letter) {
    uint32_t h = 2166136261u;
    auto mix = [&h](unsigned char b) { h = (h ^ b) * 16777619u; };
    for (unsigned char b : mod) mix(b);
    mix(0);
    for (unsigned char b : letter) mix(b);
    uint32_t k = (h >> 24) ^ (h & 0xFFFFFF);
    return k ? k : 1;
}

// A view of the 0x50-byte block (host bytes; guest memory is byte-addressed the same way).
class Book {
public:
    explicit Book(uint8_t* bytes) : b_(bytes) {}
    bool empty() const {
        for (uint32_t i = 0; i < kBookSize; i++)
            if (b_[i]) return false;
        return true;
    }
    // ours: a valid header, or still all zero (a fresh save)
    bool usable() const { return empty() || (b_[0] == kMagic && b_[1] == kVersion && !b_[2] && !b_[3]); }
    State state(uint32_t key) const {
        if (!usable()) return kNone;
        int i = find(key);
        return i < 0 ? kNone : State(b_[4 + 4 * i + 3]);
    }
    // Sets a letter's state, adding its record if needed. False when the block is not ours or full.
    bool set(uint32_t key, State s) {
        if (!usable() || !key || key > 0xFFFFFF) return false;
        int i = find(key);
        if (i < 0) {
            for (unsigned r = 0; r < kRecords && i < 0; r++)
                if (!record_key(r)) i = int(r);
            if (i < 0) return false;
        }
        b_[0] = kMagic, b_[1] = kVersion, b_[2] = 0, b_[3] = 0;
        uint8_t* p = b_ + 4 + 4 * i;
        p[0] = uint8_t(key >> 16), p[1] = uint8_t(key >> 8), p[2] = uint8_t(key), p[3] = s;
        return true;
    }
    uint32_t record_key(unsigned r) const {
        const uint8_t* p = b_ + 4 + 4 * r;
        return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }
    State record_state(unsigned r) const { return State(b_[4 + 4 * r + 3]); }

private:
    int find(uint32_t key) const {
        if (b_[0] != kMagic) return -1;
        for (unsigned r = 0; r < kRecords; r++)
            if (record_key(r) == key) return int(r);
        return -1;
    }
    uint8_t* b_;
};

inline constexpr size_t kMaxIdBytes = 32, kMaxSenderBytes = 48, kMaxTextBytes = 1200;
inline constexpr size_t kMaxUnits = 700;  // UTF-16 code units of the composed letter, NUL excluded

// Letter IDs: 1..32 of a-z 0-9 - _ .
inline bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > kMaxIdBytes) return false;
    for (char ch : id)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')) return false;
    return true;
}

// UTF-8 -> UTF-16, appended to out. False on invalid UTF-8 or a control character other than a
// line break (the game's message text uses control codes for its own tags, so a mod can't pass
// any: a letter is plain text). Tabs and CR are not allowed either.
inline bool append_utf16(const std::string& in, bool allow_newline, std::u16string& out) {
    for (size_t i = 0; i < in.size();) {
        unsigned char c0 = (unsigned char)in[i];
        uint32_t cp, n;
        if (c0 < 0x80) cp = c0, n = 1;
        else if ((c0 & 0xE0) == 0xC0) cp = c0 & 0x1F, n = 2;
        else if ((c0 & 0xF0) == 0xE0) cp = c0 & 0x0F, n = 3;
        else if ((c0 & 0xF8) == 0xF0) cp = c0 & 0x07, n = 4;
        else return false;
        if (i + n > in.size()) return false;
        for (uint32_t k = 1; k < n; k++) {
            unsigned char cx = (unsigned char)in[i + k];
            if ((cx & 0xC0) != 0x80) return false;
            cp = cp << 6 | (cx & 0x3F);
        }
        static constexpr uint32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kMin[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp == '\n') {
            if (!allow_newline) return false;
        } else if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0) || cp == 0xFEFF) {
            return false;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(char16_t(0xD800 + (cp >> 10)));
            out.push_back(char16_t(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(char16_t(cp));
        }
        i += n;
    }
    return true;
}

// The letter as the message window shows it: the text, a blank line and the sender as the
// signature. Empty when the input is not valid or too long.
inline std::u16string compose(const std::string& sender, const std::string& text) {
    std::u16string out;
    if (sender.empty() || sender.size() > kMaxSenderBytes || text.empty() || text.size() > kMaxTextBytes) return {};
    if (!append_utf16(text, true, out)) return {};
    while (!out.empty() && out.back() == u'\n') out.pop_back();
    if (out.empty()) return {};
    out += u"\n\n";
    if (!append_utf16(sender, false, out)) return {};
    if (out.size() > kMaxUnits) return {};
    return out;
}

}  // namespace story_mail
