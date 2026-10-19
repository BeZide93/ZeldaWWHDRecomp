// SPDX-License-Identifier: MPL-2.0
#include "demo_event_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace demo_event_image {
namespace {
using Bytes = std::vector<uint8_t>;
constexpr uint32_t kNone = 0xFFFFFFFFu;
constexpr uint32_t kEvent = 0x40, kStaff = 0xF0, kCut = 0x140, kProperty = 0x190;
void put(Bytes& b, size_t p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (24 - 8 * i)); }
uint32_t get(const Bytes& b, size_t p) { return uint32_t(b[p]) << 24 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 8 | b[p + 3]; }
void putf(Bytes& b, size_t p, float f) { uint32_t v; std::memcpy(&v, &f, 4); put(b, p, v); }
float getf(const Bytes& b, size_t p) { const uint32_t v = get(b, p); float f; std::memcpy(&f, &v, 4); return f; }
void name(Bytes& b, size_t p, const std::string& s) { std::memcpy(b.data() + p, s.data(), s.size()); }
bool fail(std::string& why, const char* text) { why = text; return false; }
bool plain(const std::string& s, size_t max) {
    if (s.empty() || s.size() >= max) return false;
    for (char c : s) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
    return true;
}
bool check(const Spec& s, std::string& why) {
    if (!plain(s.event, 32)) return fail(why, "demo event name must be 1..31 plain characters");
    if (!plain(s.file, 32)) return fail(why, "demo file name must be 1..31 plain characters");
    if (s.origin) {
        const auto& o = *s.origin;
        for (float v : {o.x, o.y, o.z, o.angle_y}) if (!std::isfinite(v) || std::fabs(v) > 1e6f) return fail(why, "demo origin must be finite");
    }
    return true;
}
Bytes build(const Spec& s) {
    const bool origin = s.origin.has_value();
    const uint32_t properties = origin ? 3 : 1, floats = origin ? 4 : 0;
    const uint32_t float_at = kProperty + properties * 0x40, string_at = float_at + floats * 4;
    const uint32_t string_bytes = uint32_t((s.file.size() + 1 + 15) & ~size_t(15));
    Bytes b(string_at + string_bytes, 0);
    // pools: event, staff, cut, property, float, int (none), string
    const uint32_t offsets[7] = {kEvent, kStaff, kCut, kProperty, floats ? float_at : 0, 0, string_at};
    const uint32_t counts[7] = {1, 1, 1, properties, floats, 0, string_bytes};
    for (unsigned i = 0; i < 7; ++i) { put(b, i * 8, offsets[i]); put(b, i * 8 + 4, counts[i]); }
    name(b, kEvent, s.event);
    for (unsigned i = 0; i < 20; ++i) put(b, kEvent + 0x2C + i * 4, i == 0 ? 0 : kNone);
    put(b, kEvent + 0x7C, 1);
    put(b, kEvent + 0x88, 5); put(b, kEvent + 0x8C, kNone); put(b, kEvent + 0x90, kNone);
    name(b, kStaff, "PACKAGE");
    put(b, kStaff + 0x24, 0); put(b, kStaff + 0x2C, 11); put(b, kStaff + 0x30, 0); put(b, kStaff + 0x38, kNone); put(b, kStaff + 0x3C, kNone);
    name(b, kCut, "PLAY");
    put(b, kCut + 0x20, 0);
    for (unsigned j = 0; j < 3; ++j) put(b, kCut + 0x28 + j * 4, kNone);
    put(b, kCut + 0x34, 0); put(b, kCut + 0x38, 0); put(b, kCut + 0x3C, kNone);
    struct Prop { const char* name; uint32_t type, index, count, next; };
    const Prop props[3] = {{"FileName", 4, 0, uint32_t(s.file.size() + 1), origin ? 1u : kNone}, {"OffsetPos", 1, 0, 1, 2}, {"OffsetAngY", 0, 3, 1, kNone}};
    for (unsigned i = 0; i < properties; ++i) {
        const size_t at = kProperty + i * 0x40;
        name(b, at, props[i].name);
        put(b, at + 0x20, i); put(b, at + 0x24, props[i].type); put(b, at + 0x28, props[i].index); put(b, at + 0x2C, props[i].count); put(b, at + 0x30, props[i].next);
    }
    if (origin) { putf(b, float_at, s.origin->x); putf(b, float_at + 4, s.origin->y); putf(b, float_at + 8, s.origin->z); putf(b, float_at + 12, s.origin->angle_y); }
    name(b, string_at, s.file);
    return b;
}
}  // namespace

bool make(const Spec& spec, std::vector<uint8_t>& out, std::string& why) {
    out.clear(); why.clear();
    if (!check(spec, why)) return false;
    out = build(spec);
    return true;
}

bool validate(const std::vector<uint8_t>& b, Spec& out, std::string& why) {
    out = {}; why.clear();
    if (b.size() < kProperty + 0x40 + 16 || b.size() > 0x400) return fail(why, "wrong demo event image extent");
    Spec s;
    const char* event = reinterpret_cast<const char*>(b.data() + kEvent);
    s.event.assign(event, strnlen(event, 31));
    const uint32_t properties = get(b, 3 * 8 + 4);
    if (properties != 1 && properties != 3) return fail(why, "demo event property count");
    const uint32_t string_at = get(b, 6 * 8);
    if (string_at >= b.size()) return fail(why, "demo event string pool");
    const char* file = reinterpret_cast<const char*>(b.data() + string_at);
    s.file.assign(file, strnlen(file, std::min<size_t>(31, b.size() - string_at)));
    if (properties == 3) {
        const uint32_t float_at = get(b, 4 * 8);
        if (float_at + 16 > b.size()) return fail(why, "demo event float pool");
        s.origin = Origin{getf(b, float_at), getf(b, float_at + 4), getf(b, float_at + 8), getf(b, float_at + 12)};
    }
    if (!check(s, why)) return false;
    if (b != build(s)) return fail(why, "noncanonical demo event image");
    out = s;
    return true;
}
}  // namespace demo_event_image
