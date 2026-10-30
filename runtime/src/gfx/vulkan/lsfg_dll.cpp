// SPDX-License-Identifier: MPL-2.0
// PE resource and LSFG shader interface parser adapted from SSunnKing's Enhanced fork (MPL-2.0).
#include "lsfg_dll.h"
#include "../../../../tools/wudextract/crypto.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
namespace gfxvk::fg::dll {
namespace {
constexpr uint32_t kFp16Base = 303, kFp32Base = 352;
// shader indices within a set
enum : uint32_t { S_COPY = 0, S_MIPS = 1, S_BLEND = 2, S_QUALITY = 3, S_PERF = 26 };
// bindings per shader: samplers, sampled images, storage images (checked against the DLL)
const uint8_t kIface[kShaders][3] = {
    {0, 1, 1},  {1, 1, 7},  {2, 5, 1},  {2, 9, 3},  {2, 10, 2}, {1, 3, 4}, {1, 4, 4}, {1, 4, 4},  {2, 6, 1}, {1, 3, 4},
    {1, 4, 4},  {1, 4, 4},  {2, 6, 1},  {1, 1, 2},  {1, 2, 2},  {1, 2, 4}, {1, 4, 4}, {1, 2, 2},  {1, 2, 2}, {1, 2, 2},
    {2, 3, 1},  {1, 12, 2}, {1, 2, 2},  {1, 2, 2},  {1, 2, 2},  {1, 2, 6}, {2, 5, 3}, {2, 6, 1},  {1, 3, 2}, {1, 2, 2},
    {1, 2, 2},  {2, 4, 1},  {1, 3, 2},  {1, 2, 2},  {1, 2, 2},  {2, 4, 1}, {1, 1, 1}, {1, 1, 1},  {1, 1, 2}, {1, 2, 2},
    {1, 1, 1},  {1, 1, 1},  {1, 1, 1},  {2, 2, 1},  {1, 6, 2},  {1, 2, 2}, {1, 2, 2}, {1, 2, 2},  {1, 2, 6}};

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 128 * 1024 * 1024) { fclose(f); return false; }
    out.resize(size_t(n));
    bool ok = n > 0 && fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

template <typename T> bool rd(const std::vector<uint8_t>& f, size_t off, T& v) {
    if (off > f.size() || sizeof(T) > f.size() - off) return false;
    memcpy(&v, f.data() + off, sizeof(T));
    return true;
}

// RCDATA resources of a PE file: id -> (file offset, size)
bool pe_rcdata(const std::vector<uint8_t>& f, std::map<uint32_t, std::pair<size_t, size_t>>& out, std::string& err) {
    uint32_t pe = 0, sig = 0;
    uint16_t nsec = 0, optSize = 0, magic = 0;
    if (!rd(f, 0x3c, pe) || !rd(f, pe, sig) || sig != 0x4550) return err = "not a PE file", false;
    rd(f, pe + 6, nsec);
    rd(f, pe + 20, optSize);
    size_t opt = pe + 24;
    rd(f, opt, magic);
    if (magic != 0x20b && magic != 0x10b) return err = "invalid PE optional header", false;
    size_t dirs = opt + (magic == 0x20b ? 112 : 96);
    uint32_t rsrcRva = 0, rsrcSize = 0;
    if (!rd(f, dirs + 2 * 8, rsrcRva) || !rd(f, dirs + 2 * 8 + 4, rsrcSize) || !rsrcRva) return err = "no resources", false;
    auto toOff = [&](uint32_t rva, size_t& off) {
        for (uint32_t i = 0; i < nsec; i++) {
            size_t s = opt + optSize + 40 * i;
            uint32_t vsize = 0, va = 0, rsize = 0, raw = 0;
            rd(f, s + 8, vsize);
            rd(f, s + 12, va);
            rd(f, s + 16, rsize);
            rd(f, s + 20, raw);
            if (rva >= va && rva < va + std::max(vsize, rsize)) {
                off = raw + (rva - va);
                return off < f.size();
            }
        }
        return false;
    };
    size_t root;
    if (!toOff(rsrcRva, root)) return err = "bad resource section", false;
    // directory entries: (id, offset); high bit of the offset: a subdirectory
    auto entries = [&](size_t dir, std::vector<std::pair<uint32_t, uint32_t>>& e) {
        uint16_t named = 0, ids = 0;
        if (!rd(f, dir + 12, named) || !rd(f, dir + 14, ids)) return false;
        for (uint32_t i = 0; i < (uint32_t)named + ids; i++) {
            uint32_t name = 0, off = 0;
            if (!rd(f, dir + 16 + 8 * i, name) || !rd(f, dir + 20 + 8 * i, off)) return false;
            e.push_back({name, off});
        }
        return true;
    };
    std::vector<std::pair<uint32_t, uint32_t>> types;
    if (!entries(root, types)) return err = "bad resource directory", false;
    for (auto [type, toff] : types) {
        if (type != 10 || !(toff & 0x80000000u)) continue;  // RT_RCDATA
        std::vector<std::pair<uint32_t, uint32_t>> names;
        if (!entries(root + (toff & 0x7fffffff), names)) return err = "bad resource directory", false;
        for (auto [id, noff] : names) {
            if ((id & 0x80000000u) || !(noff & 0x80000000u)) continue;
            std::vector<std::pair<uint32_t, uint32_t>> langs;
            if (!entries(root + (noff & 0x7fffffff), langs) || langs.empty()) continue;
            size_t data = root + (langs[0].second & 0x7fffffff);
            uint32_t rva = 0, size = 0;
            size_t off;
            if (!rd(f, data, rva) || !rd(f, data + 4, size) || !toOff(rva, off) || off + size > f.size()) continue;
            out[id] = {off, size};
        }
    }
    if (out.empty()) return err = "no RCDATA resources", false;
    return true;
}

// Collects the descriptor bindings and makes the images' declared formats match ours: the copy
// declares rgba32f and the blend leaves the format open (needs WriteWithoutFormat), both write rgba8.
bool prepare_spirv(Shader& s, bool rgba8Output, std::string& err) {
    auto& w = s.spirv;
    if (w.size() < 5 || w[0] != 0x07230203) return err = "not SPIR-V", false;
    for (size_t p = 5; p < w.size();) {
        uint32_t op = w[p] & 0xffff, n = w[p] >> 16;
        if (!n || p + n > w.size()) return err = "malformed SPIR-V", false;
        if (op == 71 /*OpDecorate*/ && n >= 4 && w[p + 2] == 33 /*Binding*/) s.bindings.push_back(w[p + 3]);
        if (rgba8Output && op == 25 /*OpTypeImage*/ && n >= 9 && w[p + 7] == 2 /*storage*/) w[p + 8] = 4 /*Rgba8*/;
        if (rgba8Output && op == 17 /*OpCapability*/ && n == 2 && w[p + 1] == 56 /*StorageImageWriteWithoutFormat*/)
            w[p + 1] = 1;  // Shader (declared already; a repeat is harmless)
        p += n;
    }
    std::sort(s.bindings.begin(), s.bindings.end());
    return true;
}

// one shader set (base: first resource id) into `out`; false and `err` if it doesn't match
bool read_set(const std::vector<uint8_t>& file, const std::map<uint32_t, std::pair<size_t, size_t>>& res, uint32_t base,
              Shader* out, std::string& err) {
    for (uint32_t i = 0; i < kShaders; i++) {
        auto it = res.find(base + i);
        Shader& s = out[i];
        s = Shader{};
        if (it == res.end() || it->second.second > 4 * 1024 * 1024 || it->second.second % 4) {
            err = "shader " + std::to_string(base + i) + " missing: unsupported Lossless.dll version";
            return false;
        }
        s.spirv.resize(it->second.second / 4);
        memcpy(s.spirv.data(), file.data() + it->second.first, it->second.second);
        if (!prepare_spirv(s, i == S_COPY || i == S_BLEND, err)) {
            err = "shader " + std::to_string(base + i) + ": " + err;
            return false;
        }
        uint32_t n[3] = {};
        for (uint32_t b : s.bindings)
            if (b >= 16) n[std::min(2u, (b - 16) / 16)]++;
        if (n[0] != kIface[i][0] || n[1] != kIface[i][1] || n[2] != kIface[i][2]) {
            err = "shader " + std::to_string(base + i) + " has an unexpected interface: unsupported Lossless.dll version";
            return false;
        }
    }
    return true;
}

bool read_dll(const std::string& path, std::vector<uint8_t>& file, std::map<uint32_t, std::pair<size_t, size_t>>& res,
              std::string& err) {
    if (!read_file(path, file)) return err = "cannot read the file", false;
    return pe_rcdata(file, res, err);
}

std::string shader_fingerprint(const std::vector<uint8_t>& file, const std::map<uint32_t, std::pair<size_t, size_t>>& res) {
    std::vector<uint8_t> all;
    for (uint32_t id = kFp16Base; id < kFp32Base + kShaders; id++) {
        auto it = res.find(id);
        if (it == res.end()) return "";
        if (it->second.second > 32 * 1024 * 1024 - all.size()) return "";
        all.insert(all.end(), file.begin() + it->second.first, file.begin() + it->second.first + it->second.second);
    }
    uint8_t h[20];
    wudcrypto::sha1(all.data(), all.size(), h);
    char hex[41];
    for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    return hex;
}

// shader versions this code was built and tested against (the pass graph was recovered from them)
const char* const kTestedShaders[] = {
    "fc6092a72b94003fac3d68e9a7b54954422db757",  // the Lossless.dll this was developed with (7,521,280 bytes)
};

}
bool read(const std::string& path, Shader* out, std::string& error) {
    std::vector<uint8_t> file;
    std::map<uint32_t, std::pair<size_t,size_t>> resources;
    if (!read_dll(path, file, resources, error)) return false;
    const auto fingerprint = shader_fingerprint(file, resources);
    for (const char* tested : kTestedShaders)
        if (fingerprint == tested) return read_set(file, resources, kFp32Base, out, error);
    error = "Unsupported Lossless.dll shader version (expected LSFG 3 shader set fc6092a7).";
    return false;
}
}
