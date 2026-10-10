// Private setup-generated cache generations. The release template contains no
// code; these files are resolved from the player's game and stay on this device.
#pragma once
#include "platform/host.h"
#include "platform/process.h"
#include "mods/mod_json.h"
#include "mods/mod_hash.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <cstdio>
#include <chrono>
#ifndef _WIN32
#include <sys/file.h>
#endif

namespace shaderprebuild {
namespace fs = std::filesystem;
inline std::string runtimeLibrary;
inline std::string resultPath;
inline mods::json::Value pendingIdentity;
inline bool pendingBound = false;
inline fs::path runtime_path() { return runtimeLibrary.empty() ? host::executable_path() : runtimeLibrary; }
inline fs::path root() { return fs::path(host::config_dir()) / "shader-prebuild"; }
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
        auto path = host::process_wide((root() / "activation.lock").string());
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
        file << "Rebuild shaders and pipelines on the next start.\n";
        return bool(file);
    } catch (...) { return false; }
}
inline void set_default(const char* name, const fs::path& value) {
    if (std::getenv(name)) return;
#ifdef _WIN32
    _putenv_s(name, value.string().c_str());
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
inline bool identity_matches(const mods::json::Value& active, const fs::path& game,
                             const std::string& renderer) {
    const auto& id = active.get("identity");
    if (id.get("schema").number != 1 || id.get("renderer").string() != renderer ||
        id.get("runtime").string() != mods::hash::sha256_file(runtime_path())) return false;
    const auto launcher = read(root() / "launcher.json");
    const fs::path package = launcher.get("package").string();
    if (id.get("template").string() != mods::hash::sha256_file(fs::path(launcher.get("template").string())) ||
        id.get("translator").string() != mods::hash::sha256_file(package / "tools/shaderprep.py") ||
        id.get("format").string() != mods::hash::sha256_file(package / "tools/shaderprep_format.py")) return false;
    const auto& inventory = id.get("game");
    if (inventory.type != mods::json::Value::Object || inventory.object.empty()) return false;
    size_t count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(game)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".rpx" && ext != ".sharcfb" && ext != ".szs" && ext != ".pack" && ext != ".sarc") continue;
        ++count;
        const auto key = entry.path().lexically_relative(game).generic_string();
        if (inventory.get(key).string() != mods::hash::sha256_file(entry.path())) return false;
    }
    return count == inventory.object.size();
}
// A deliberate skip is tied to the full game/runtime/template identity. A
// later rebuild request or version change makes it obsolete.
inline bool skipped(const fs::path& game, const std::string& renderer) {
    if (requested()) return false;
    try {
        const auto marker = read(root() / "skip.json");
        return marker.get("schema").number == 1 && identity_matches(marker, game, renderer);
    } catch (...) { return false; }
}
inline void bind_generation(const fs::path& dir) {
    set_default("WWHD_HEADSTART", dir / "headstart.bin");
    set_default("WWHD_SHADER_CACHE", dir / "shaders.bin");
    set_default("WWHD_VK_SHADER_CACHE", dir / "vulkan-shaders");
    set_default("WWHD_VK_PIPELINE_CACHE", dir / "vulkan-pipelines.bin");
}
inline bool activate_pending(const fs::path& game, const std::string& renderer) {
    if (std::getenv("WWHD_HEADSTART") || skipped(game, renderer)) return false;
    try {
        const auto active = read(root() / "active.json");
        if (active.get("status").string() != "warm_pending" || !identity_matches(active, game, renderer)) return false;
        const auto dir = generation(active);
        if (!fs::is_regular_file(dir / "headstart.bin")) return false;
        bind_generation(dir); pendingIdentity = active; pendingBound = true; return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] Pending preparation unavailable: %s\n", e.what()); return false;
    }
}
inline bool atomic_json(const fs::path& path, const mods::json::Value& value) {
    fs::create_directories(path.parent_path());
    const auto temporary = path.string() + ".native-new";
    { std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      file << mods::json::dump(value) << '\n'; if (!file) return false; }
    return host::replace_file(temporary, path.string());
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
                if (success) { std::error_code ec; fs::remove(root() / "rebuild.request", ec); }
            }
        }
        if (!resultPath.empty()) {
            mods::json::Value result; result["schema"] = 1;
            result["status"] = success ? "ready" : "failed";
            success = atomic_json(resultPath, result) && success;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] Cannot publish preparation result: %s\n", e.what()); return false;
    }
    return success;
}
// Call before renderer initialization. Explicit test/user cache paths win. A
// rebuild publishes a fresh generation; live caches are never deleted by UI.
inline bool activate(const fs::path& game, const std::string& renderer) {
    if (std::getenv("WWHD_HEADSTART") || skipped(game, renderer)) return false;
    try {
        bool valid = false;
        mods::json::Value active;
        try {
            active = read(root() / "active.json");
            valid = !requested() && active.get("status").string() == "ready" &&
                identity_matches(active, game, renderer) && fs::is_regular_file(generation(active) / "headstart.bin");
        } catch (...) { valid = false; }
#ifndef __ANDROID__
        if (!valid) {
            const auto launcher = read(root() / "launcher.json");
            const fs::path package = launcher.get("package").string();
            const fs::path helper = package / "tools/installer/shader_prebuild.py";
            const fs::path templatePath = launcher.get("template").string();
            if (launcher.get("schema").number != 1 || launcher.get("helper").string() != helper.string() ||
                templatePath.empty() || !fs::is_regular_file(helper))
                throw std::runtime_error("shader preparation launcher unavailable");
            if (!fs::is_regular_file(templatePath)) {
                std::fprintf(stderr, "[shaders] Shader template unavailable; shaders compile during play.\n");
                return false;
            }
            std::fprintf(stderr, "[shaders] Preparing shaders; please wait.\n");
            auto result = host::run_process({launcher.get("python").string(), helper.string(),
                "--runtime-prepare", "--game", game.string(), "--template", templatePath.string(),
                "--executable", host::executable_path(), "--cache-root", root().string(),
                "--package", package.string(), "--renderer", renderer});
            std::fprintf(stderr, "%s", result.output.c_str());
            if (result.code) throw std::runtime_error("shader preparation failed; using on-demand compilation");
            const auto last = result.output.find_last_not_of("\r\n");
            if (last == std::string::npos) throw std::runtime_error("shader preparation returned no status");
            const auto line = result.output.rfind('\n', last);
            const auto status = mods::json::parse(result.output.substr(
                line == std::string::npos ? 0 : line + 1,
                last - (line == std::string::npos ? 0 : line + 1) + 1)).get("status").string();
            if (status == "skipped" || skipped(game, renderer)) return false;
            if (status != "ready") throw std::runtime_error("shader preparation returned invalid status");
            active = read(root() / "active.json");
            valid = active.get("status").string() == "ready" && identity_matches(active, game, renderer);
        }
#endif
        if (!valid) return false;
        const auto dir = generation(active);
        if (!fs::is_regular_file(dir / "headstart.bin")) return false;
        bind_generation(dir);
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[shaders] %s\n", e.what()); return false;
    }
}
} // namespace shaderprebuild
