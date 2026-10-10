// Private setup-generated cache generations. The release template contains no
// code; these files are resolved from the player's game and stay on this device.
#pragma once
#include "platform/host.h"
#include "platform/process.h"
#include "mods/mod_json.h"
#include "mods/mod_hash.h"
#include "gfx/vulkan/atomic_temporary.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <cstdio>
#include <chrono>
#include <sys/stat.h>
#include <memory>
#ifndef _WIN32
#include <sys/file.h>
#endif

namespace shaderprebuild {
namespace fs = std::filesystem;
inline std::string runtimeLibrary;
inline std::string resultPath;
inline mods::json::Value pendingIdentity;
inline bool pendingBound = false;
inline fs::path runtime_path() { return fs::u8path(runtimeLibrary.empty() ? host::executable_path() : runtimeLibrary); }
inline fs::path root() { return fs::u8path(host::config_dir()) / "shader-prebuild"; }
class PublicationLock {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    OVERLAPPED offset{};
#else
    int file = -1;
#endif
public:
    PublicationLock() {
        fs::create_directories(root());
#ifdef _WIN32
        auto path = (root() / "activation.lock").wstring();
        file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open shader activation lock");
#else
        file = open((root() / "activation.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (file < 0) throw std::runtime_error("cannot open shader activation lock");
#endif
        for (int attempt = 0; attempt < 500; ++attempt) {
#ifdef _WIN32
            if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset)) return;
#else
            if (!flock(file, LOCK_EX | LOCK_NB)) return;
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#ifdef _WIN32
        CloseHandle(file); file = INVALID_HANDLE_VALUE;
#else
        close(file); file = -1;
#endif
        throw std::runtime_error("shader activation lock busy");
    }
    PublicationLock(const PublicationLock&) = delete;
    PublicationLock& operator=(const PublicationLock&) = delete;
    ~PublicationLock() {
#ifdef _WIN32
        if (file != INVALID_HANDLE_VALUE) { UnlockFileEx(file, 0, 1, 0, &offset); CloseHandle(file); }
#else
        if (file >= 0) { flock(file, LOCK_UN); close(file); }
#endif
    }
};
inline mods::json::Value read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("shader preparation manifest unavailable");
    std::string text; char block[4096];
    while (file.read(block, sizeof block) || file.gcount()) {
        text.append(block, size_t(file.gcount()));
        if (text.size() > 1024 * 1024) throw std::runtime_error("shader preparation manifest too large");
    }
    return mods::json::parse(text);
}
inline bool requested() { return fs::exists(root() / "rebuild.request"); }
inline bool request_rebuild() {
    try {
        fs::create_directories(root());
        std::ofstream file(root() / "rebuild.request", std::ios::trunc);
        file << "Prepare shaders optionally using setup repair.\n";
        return bool(file);
    } catch (...) { return false; }
}
inline void set_default(const char* name, const fs::path& value) {
    if (std::getenv(name)) return;
#ifdef _WIN32
    _wputenv_s(host::process_wide(name).c_str(), value.c_str());
#else
    setenv(name, value.string().c_str(), 0);
#endif
}
inline fs::path generation(const mods::json::Value& active) {
    const auto name = active.get("generation").string();
    if (active.get("schema").number != 1 || name.empty() ||
        name.rfind("generation-", 0) || fs::path(name).filename() != fs::path(name) ||
        name.find('\\') != std::string::npos || name.find('\0') != std::string::npos)
        throw std::runtime_error("invalid shader cache generation");
    auto path = root() / name;
    if (fs::is_symlink(path)) throw std::runtime_error("shader cache generation is a symlink");
    return path;
}
// Metadata is a launch freshness check, not a content authenticator. Full hashes
// are produced only by explicit setup/rebuild. Missing legacy metadata fails closed.
inline mods::json::Value metadata(const fs::path& path) {
    mods::json::Value value;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        throw std::runtime_error("cache input unavailable");
    const uint64_t ticks = (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    const uint64_t unixTicks = ticks - 116444736000000000ull;
    value["size"] = double((uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
    value["mtime"] = double(unixTicks / 10000000);
    value["mtime_ns"] = double((unixTicks % 10000000) * 100);
#else
    struct stat info{};
    if (stat(path.c_str(), &info)) throw std::runtime_error("cache input unavailable");
    value["size"] = double(info.st_size); value["mtime"] = double(info.st_mtime);
#ifdef __APPLE__
    value["mtime_ns"] = double(info.st_mtimespec.tv_nsec);
#else
    value["mtime_ns"] = double(info.st_mtim.tv_nsec);
#endif
#endif
    return value;
}
inline bool identity_matches(const mods::json::Value& active, const fs::path& game,
                             const std::string& renderer, bool optOut = false) {
    const auto& id = active.get("identity");
    const auto& launch = active.get("launch");
    if (id.get("schema").number != 1 || (!optOut && id.get("renderer").string() != renderer)) return false;
    const auto launcher = read(root() / "launcher.json");
    const fs::path package = fs::u8path(launcher.get("package").string());
    if (!(launch.get("template") == metadata(fs::u8path(launcher.get("template").string())))) return false;
    if (!optOut && (!(launch.get("runtime") == metadata(runtime_path())) ||
        !(launch.get("translator") == metadata(package / "tools/shaderprep.py")) ||
        !(launch.get("format") == metadata(package / "tools/shaderprep_format.py")))) return false;
    const auto& inventory = launch.get("game");
    if (inventory.type != mods::json::Value::Object || inventory.object.empty()) return false;
    size_t count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(game)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".rpx" && ext != ".sharcfb" && ext != ".szs" && ext != ".pack" && ext != ".sarc") continue;
        ++count;
        const auto bytes = entry.path().lexically_relative(game).generic_u8string();
        const std::string key(bytes.begin(), bytes.end());
        if (!(inventory.get(key) == metadata(entry.path()))) return false;
    }
    return count == inventory.object.size();
}
// Opt-out survives runtime/translator updates; game/template changes or an
// explicit rebuild request permit preparation in setup again.
inline bool skipped(const fs::path& game, const std::string& renderer) {
    if (requested()) return false;
    try {
        const auto marker = read(root() / "skip.json");
        return marker.get("schema").number == 1 && identity_matches(marker, game, renderer, true);
    } catch (...) { return false; }
}
// Keep selected headstart files alive while another setup switches generations.
class GenerationLease {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    OVERLAPPED offset{};
#else
    int file = -1;
#endif
public:
    bool acquire(const fs::path& dir) {
#ifdef _WIN32
        file = CreateFileW((dir / "in-use.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return file != INVALID_HANDLE_VALUE && LockFileEx(file, LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset);
#else
        file = open((dir / "in-use.lock").c_str(), O_CREAT | O_RDWR, 0600);
        return file >= 0 && flock(file, LOCK_SH | LOCK_NB) == 0;
#endif
    }
    ~GenerationLease() {
#ifdef _WIN32
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
#else
        if (file >= 0) close(file);
#endif
    }
};
inline std::unique_ptr<GenerationLease> generationLease;
inline void bind_live_caches() {
    set_default("WWHD_SHADER_CACHE", root() / "live/shaders.bin");
    set_default("WWHD_VK_SHADER_CACHE", root() / "live/vulkan-shaders");
    set_default("WWHD_VK_PIPELINE_CACHE", root() / "live/vulkan-pipelines.bin");
}
inline void bind_generation(const fs::path& dir) {
    set_default("WWHD_HEADSTART", dir / "headstart.bin");
    bind_live_caches();
}
inline bool activate_pending(const fs::path& game, const std::string& renderer) {
    bind_live_caches();
    if (std::getenv("WWHD_HEADSTART") || skipped(game, renderer)) return false;
    try {
        const auto active = read(root() / "active.json");
        if (active.get("status").string() != "warm_pending" || !identity_matches(active, game, renderer)) return false;
        const auto dir = generation(active);
        if (!fs::is_regular_file(dir / "headstart.bin")) return false;
        auto lease = std::make_unique<GenerationLease>();
        if (!lease->acquire(dir)) return false;
        generationLease = std::move(lease);
        bind_generation(dir); pendingIdentity = active; pendingBound = true; return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] Pending preparation unavailable: %s\n", e.what()); return false;
    }
}
inline bool atomic_json(const fs::path& path, const mods::json::Value& value) {
    fs::create_directories(path.parent_path());
    const auto bytes = path.u8string();
    const std::string destination(bytes.begin(), bytes.end());
    std::string temporary;
    FILE* file = gfxvk::open_atomic_temporary(destination, temporary);
    if (!file) return false;
    const auto text = mods::json::dump(value) + '\n';
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    const bool success = written && closed && host::replace_file(temporary, destination);
    if (!success) { std::error_code ec; fs::remove(fs::u8path(temporary), ec); }
    return success;
}
inline void cleanup_generations(const fs::path& selected) {
    try {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root(), ec)) {
        const auto dir = entry.path();
        if (dir == selected || entry.is_symlink() || !entry.is_directory() ||
            dir.filename().string().rfind("generation-", 0) != 0 ||
            !fs::is_regular_file(dir / "manifest.json")) continue;
#ifdef _WIN32
        HANDLE file = CreateFileW((dir / "in-use.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        OVERLAPPED offset{};
        const bool available = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset);
        CloseHandle(file);
        if (available) fs::remove_all(dir, ec);
#else
        const int file = open((dir / "in-use.lock").c_str(), O_CREAT | O_RDWR, 0600);
        if (file < 0) continue;
        if (flock(file, LOCK_EX | LOCK_NB) == 0) fs::remove_all(dir, ec);
        close(file);
#endif
    }
    } catch (const fs::filesystem_error&) { /* Cleanup never invalidates a successful switch. */ }
}
inline bool finish_pending(bool success) {
    try {
        if (success) {
            PublicationLock lock;
            auto active = read(root() / "active.json");
            const auto dir = generation(active);
            // Commit the generation before publishing ready to the setup UI.
            if (!pendingBound || !(active == pendingIdentity) || active.get("status").string() != "warm_pending") success = false;
            else {
                active["status"] = "ready";
                success = atomic_json(dir / "manifest.json", active) && atomic_json(root() / "active.json", active);
                if (success) { std::error_code ec; fs::remove(root() / "rebuild.request", ec); fs::remove(root() / "failed.json", ec); fs::remove(root() / "skip.json", ec); cleanup_generations(dir); }
            }
        }
        if (!success && pendingBound) {
            auto failed = pendingIdentity;
            failed["status"] = "failed";
            atomic_json(root() / "failed.json", failed);
        }
        if (!resultPath.empty()) {
            mods::json::Value result; result["schema"] = 1;
            result["status"] = success ? "ready" : "failed";
            success = atomic_json(fs::u8path(resultPath), result) && success;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] Cannot publish preparation result: %s\n", e.what()); return false;
    }
    return success;
}
// Call before renderer initialization. Explicit test/user cache paths win. A
// rebuild publishes a fresh generation; live caches are never deleted by UI.
inline bool activate(const fs::path& game, const std::string& renderer) {
    // Live caches survive invalidation/rebuild and remain useful without a
    // prepared generation. Explicit user/test overrides still win.
    bind_live_caches();
    if (std::getenv("WWHD_HEADSTART") || skipped(game, renderer)) return false;
    try {
        const auto active = read(root() / "active.json");
        // Launch never assembles or warms shaders. Updates/rebuilds belong to
        // setup's visible, cancellable preparation step.
        if (requested() || active.get("status").string() != "ready" ||
            !identity_matches(active, game, renderer)) return false;
        const auto dir = generation(active);
        if (!fs::is_regular_file(dir / "headstart.bin")) return false;
        auto lease = std::make_unique<GenerationLease>();
        if (!lease->acquire(dir)) return false;
        generationLease = std::move(lease);
        bind_generation(dir);
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] %s\n", e.what()); return false;
    }
}
} // namespace shaderprebuild
