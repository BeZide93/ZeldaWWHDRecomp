#include "shader_compiler.h"
#include "stutter_trace.h"
#include "platform/host.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <deque>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace gfxvk::vk {
namespace {
std::mutex glslangMutex; // Outlives the function-static worker.
static std::vector<uint32_t> compile_stage(const std::string& source, EShLanguage stage, std::string* error) {
    // Serialize even diagnostic inline calls against background work.
    std::lock_guard lock(glslangMutex);
    static std::once_flag init;
    static bool initialized = false;
    std::call_once(init, [] { initialized = glslang::InitializeProcess(); });
    if (error) error->clear();
    auto fail = [&](const std::string& reason) { if (error) *error = reason; return std::vector<uint32_t>{}; };
    if (!initialized) return fail("glslang initialization failed");
    glslang::TShader shader(stage);
    const char* text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
        return fail(std::string(shader.getInfoLog()) + shader.getInfoDebugLog());
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) return fail(program.getInfoLog());
    std::vector<uint32_t> words;
    glslang::SpvOptions options;
    options.disableOptimizer = true; // no SPIRV-Tools runtime dependency
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, &options);
    if (words.empty()) return fail("glslang emitted empty SPIR-V");
    return words;
}

ShaderCompileResult execute(const std::string& source, ShaderStage stage, uint64_t frame, uint64_t key) {
    EShLanguage language;
    switch (stage) {
    case ShaderStage::Vertex: language = EShLangVertex; break;
    case ShaderStage::Fragment: language = EShLangFragment; break;
    case ShaderStage::Compute: language = EShLangCompute; break;
    default: throw std::runtime_error("invalid shader compiler stage");
    }
    ShaderCompileResult result;
    const auto start = stutter::now();
    result.words = compile_stage(source, language, &result.error);
    const auto end = stutter::now();
    result.compileNs = end - start;
    result.thread = uint64_t(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    stutter::trace().add(frame == ~uint64_t{0} ? "utility_spirv_compile" : "spirv_compile",
                         frame, key, start, end, result.words.empty() ? -1 : 0);
    return result;
}
struct Job {
    std::string source;
    ShaderStage stage;
    uint64_t frame, key;
    std::promise<ShaderCompileResult> done;
};
struct Compiler {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Job>> queue;
    std::thread thread;
    bool stopped = false;
    std::atomic<unsigned> active{0};
    void finish() {
        { std::lock_guard lock(mutex); stopped = true; }
        wake.notify_all();
        if (thread.joinable()) thread.join();
    }
    ~Compiler() { finish(); }
};
Compiler& compiler() {
    (void)stutter::trace(); // Trace outlives the worker at process exit.
    static Compiler value;
    return value;
}
void run(Compiler& c) {
    host::set_thread_name("Vulkan SPIR-V");
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock lock(c.mutex);
            c.wake.wait(lock, [&] { return c.stopped || !c.queue.empty(); });
            if (c.queue.empty()) return;
            job = std::move(c.queue.front()); c.queue.pop_front();
        }
        ShaderCompileResult result;
        std::exception_ptr error;
        try { result = execute(job->source, job->stage, job->frame, job->key); }
        catch (...) { error = std::current_exception(); }
        c.active.fetch_sub(1, std::memory_order_release);
        if (error) job->done.set_exception(error);
        else job->done.set_value(std::move(result));
    }
}
}
ShaderCompileResult compile_shader(const std::string& source, ShaderStage stage,
    uint64_t frame, uint64_t key, const std::function<void()>& whilePending, bool background) {
    // Eight queued sources plus one active source bound compiler input storage
    // to 36 MiB. Normal rendering has one ordered caller, one outstanding job.
    if (source.size() > 4 * 1024 * 1024)
        throw std::runtime_error("shader compiler source exceeds 4 MiB");
    if (!background) return execute(source, stage, frame, key);
    auto& c = compiler();
    auto job = std::make_shared<Job>(Job{source, stage, frame, key, {}});
    auto done = job->done.get_future();
    const auto requested = stutter::trace().enabled() ? stutter::now() : 0;
    {
        std::lock_guard lock(c.mutex);
        if (c.stopped && c.thread.joinable())
            throw std::runtime_error("shader compiler shutdown in progress");
        if (!c.thread.joinable()) { c.stopped = false; c.thread = std::thread(run, std::ref(c)); }
        if (c.queue.size() >= 8) throw std::runtime_error("shader compiler queue capacity exceeded");
        c.queue.push_back(job);
        c.active.fetch_add(1, std::memory_order_release);
        if (requested) stutter::trace().add("shader_request", frame, key, requested, requested, int(c.queue.size()));
    }
    c.wake.notify_one();
    std::exception_ptr presentationError;
    while (done.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
        if (!presentationError && whilePending) try { whilePending(); }
        catch (...) { presentationError = std::current_exception(); }
    }
    auto result = done.get();
    if (requested) stutter::trace().add("shader_pending", frame, key, requested, stutter::now(), result.words.empty() ? -1 : 0);
    if (presentationError) std::rethrow_exception(presentationError);
    return result;
}
bool async_shaders_enabled() {
    static const bool enabled = [] {
        const auto* value = std::getenv("WWHD_VK_ASYNC_SHADERS");
        return !value || std::strcmp(value, "0");
    }();
    return enabled;
}
bool shader_compiler_active() { return compiler().active.load(std::memory_order_acquire) != 0; }
void finish_shader_compiler() { compiler().finish(); }
}
