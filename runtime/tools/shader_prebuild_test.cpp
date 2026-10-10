// Synthetic files only: activation/invalidation must never touch real caches.
#include "gfx/shader_prebuild.h"
#include <cassert>
#include <chrono>
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--measure-launch") {
        const auto active = shaderprebuild::read(shaderprebuild::root() / "active.json");
        for (int i = 0; i < 3; ++i) {
            const auto start = std::chrono::steady_clock::now();
            const bool valid = shaderprebuild::identity_matches(active, std::filesystem::u8path(argv[2]), "vulkan");
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::printf("metadata_check_ms=%.3f valid=%d\n", elapsed, valid);
            if (!valid) return 2;
        }
        return 0;
    }
    // If startup unexpectedly spawns this test as its helper, leave evidence.
    if (argc > 1) {
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--cache-root") {
                std::ofstream(std::filesystem::path(argv[i + 1]) / "unexpected-spawn") << "spawned";
                return 3;
            }
        return 3;
    }
    namespace fs = std::filesystem;
    using mods::json::Value;
    const auto temporary = fs::temp_directory_path() / ("wwhd-prebuild-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#ifdef _WIN32
    _putenv_s("WWHD_USER_DIR", temporary.string().c_str());
    _putenv_s("WWHD_HEADSTART", "");
#else
    setenv("WWHD_USER_DIR", temporary.string().c_str(), 1);
    unsetenv("WWHD_HEADSTART");
#endif
    const auto package = temporary / "package", game = temporary / "game";
    auto write = [](const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path()); std::ofstream(path) << text;
    };
    auto json = [&](const fs::path& path, const Value& v) { write(path, mods::json::dump(v)); };
    write(package / "tools/shadercache/template.bin", "synthetic state fixture");
    write(package / "tools/shaderprep.py", "synthetic translator fixture");
    write(package / "tools/shaderprep_format.py", "synthetic schema fixture");
    write(game / "code/test.rpx", "synthetic executable fixture");
    Value launcher; launcher["schema"] = 1; launcher["package"] = package.string();
    launcher["helper"] = (package / "tools/installer/shader_prebuild.py").string();
    launcher["template"] = (package / "tools/shadercache/template.bin").string();
    json(shaderprebuild::root() / "launcher.json", launcher);
    Value active; active["schema"] = 1; active["status"] = "ready";
    active["generation"] = "generation-test";
    Value id; id["schema"] = 1; id["renderer"] = "vulkan";
    id["runtime"] = mods::hash::sha256_file(host::executable_path());
    id["template"] = mods::hash::sha256_file(package / "tools/shadercache/template.bin");
    id["translator"] = mods::hash::sha256_file(package / "tools/shaderprep.py");
    id["format"] = mods::hash::sha256_file(package / "tools/shaderprep_format.py");
    id["game"]["code/test.rpx"] = mods::hash::sha256_file(game / "code/test.rpx");
    active["identity"] = id;
    Value launch;
    launch["runtime"] = shaderprebuild::metadata(shaderprebuild::runtime_path());
    launch["template"] = shaderprebuild::metadata(package / "tools/shadercache/template.bin");
    launch["translator"] = shaderprebuild::metadata(package / "tools/shaderprep.py");
    launch["format"] = shaderprebuild::metadata(package / "tools/shaderprep_format.py");
    launch["game"]["code/test.rpx"] = shaderprebuild::metadata(game / "code/test.rpx");
    active["launch"] = launch;
    write(shaderprebuild::root() / "generation-test/headstart.bin", "synthetic local cache");
    json(shaderprebuild::root() / "active.json", active);
    assert(shaderprebuild::identity_matches(active, game, "vulkan"));
    assert(!shaderprebuild::identity_matches(active, game, "metal"));
    assert(shaderprebuild::activate(game, "vulkan"));
    assert(std::string(getenv("WWHD_HEADSTART")) == (shaderprebuild::root() / "generation-test/headstart.bin").string());
    // Selected external templates participate in identity; bundled changes do not.
    const auto externalTemplate = temporary / "external-template.bin";
    write(externalTemplate, "synthetic state fixture");
    active["launch"]["template"] = shaderprebuild::metadata(externalTemplate);
    launcher["template"] = externalTemplate.string();
    launcher["python"] = host::executable_path();
    json(shaderprebuild::root() / "launcher.json", launcher);
    write(package / "tools/shadercache/template.bin", "different bundled fixture");
    assert(shaderprebuild::identity_matches(active, game, "vulkan"));
    write(externalTemplate, "changed external fixture");
    assert(!shaderprebuild::identity_matches(active, game, "vulkan"));
    write(package / "tools/installer/shader_prebuild.py", "synthetic helper fixture");
    fs::remove(externalTemplate);
#ifdef _WIN32
    _putenv_s("WWHD_HEADSTART", "");
#else
    unsetenv("WWHD_HEADSTART");
#endif
    assert(!shaderprebuild::activate(game, "vulkan"));
    assert(!fs::exists(shaderprebuild::root() / "unexpected-spawn"));
    launcher["template"] = (package / "tools/shadercache/template.bin").string();
    json(shaderprebuild::root() / "launcher.json", launcher);
    write(package / "tools/shadercache/template.bin", "synthetic state fixture");
    active["launch"]["template"] = shaderprebuild::metadata(package / "tools/shadercache/template.bin");
    // A new archive and changed code both invalidate; no old generation removed.
    write(game / "content/new.pack", "synthetic archive fixture");
    assert(!shaderprebuild::identity_matches(active, game, "vulkan"));
    fs::remove(game / "content/new.pack");
    write(game / "code/test.rpx", "changed synthetic executable fixture");
    assert(!shaderprebuild::identity_matches(active, game, "vulkan"));
    active["generation"] = "../outside";
    bool rejected = false;
    try { shaderprebuild::generation(active); } catch (...) { rejected = true; }
    assert(rejected);
    assert(shaderprebuild::request_rebuild() && shaderprebuild::requested());
    assert(fs::is_regular_file(shaderprebuild::root() / "generation-test/headstart.bin"));
    write(game / "code/test.rpx", "synthetic executable fixture");
    active["launch"]["game"]["code/test.rpx"] = shaderprebuild::metadata(game / "code/test.rpx");
    active["generation"] = "generation-test"; active["status"] = "warm_pending";
    json(shaderprebuild::root() / "active.json", active);
#ifdef _WIN32
    _putenv_s("WWHD_HEADSTART", "");
#else
    unsetenv("WWHD_HEADSTART");
#endif
    shaderprebuild::resultPath = (temporary / "job/result.json").string();
    // Deliberate cancellation survives a pending generation, but rebuild wins.
    fs::remove(shaderprebuild::root() / "rebuild.request");
    Value skip; skip["schema"] = 1; skip["identity"] = id; skip["launch"] = active.get("launch");
    json(shaderprebuild::root() / "skip.json", skip);
    assert(shaderprebuild::skipped(game, "vulkan"));
    assert(!shaderprebuild::activate_pending(game, "vulkan"));
    assert(!shaderprebuild::activate(game, "vulkan"));
    assert(shaderprebuild::skipped(game, "metal"));
    auto runtimeChangedSkip = skip;
    runtimeChangedSkip["launch"]["runtime"]["size"] = 0;
    runtimeChangedSkip["launch"]["translator"]["size"] = 0;
    json(shaderprebuild::root() / "skip.json", runtimeChangedSkip);
    assert(shaderprebuild::skipped(game, "vulkan"));
    assert(!shaderprebuild::identity_matches(runtimeChangedSkip, game, "vulkan"));
    assert(shaderprebuild::request_rebuild());
    assert(!shaderprebuild::skipped(game, "vulkan"));
    fs::remove(shaderprebuild::root() / "skip.json");
    assert(shaderprebuild::activate_pending(game, "vulkan"));
    assert(!shaderprebuild::finish_pending(false));
    assert(shaderprebuild::read(shaderprebuild::resultPath).get("status").string() == "failed");
    assert(shaderprebuild::read(shaderprebuild::root() / "active.json").get("status").string() == "warm_pending");
    assert(shaderprebuild::requested());
    auto newer = active; newer["generation"] = "generation-newer";
    json(shaderprebuild::root() / "active.json", newer);
    assert(!shaderprebuild::finish_pending(true));
    assert(shaderprebuild::read(shaderprebuild::root() / "active.json").get("generation").string() == "generation-newer");
    json(shaderprebuild::root() / "active.json", active);
    assert(shaderprebuild::finish_pending(true));
    assert(shaderprebuild::read(shaderprebuild::root() / "active.json").get("status").string() == "ready");
    assert(shaderprebuild::read(shaderprebuild::resultPath).get("status").string() == "ready");
    assert(!shaderprebuild::requested());
    fs::remove_all(temporary);
}
