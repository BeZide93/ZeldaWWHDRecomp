#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gfxvk::vk {
enum class ShaderStage { Vertex, Fragment, Compute };
struct ShaderCompileResult {
    std::vector<uint32_t> words;
    std::string error;
    uint64_t compileNs = 0, thread = 0;
};
// Only the immutable source and stage enter the compiler. No guest memory,
// register state, shader-cache maps, or Vulkan handles are borrowed by a job.
ShaderCompileResult compile_shader(const std::string& source, ShaderStage stage,
    uint64_t frame, uint64_t key, const std::function<void()>& whilePending = {},
    bool background = true);
bool async_shaders_enabled();
bool shader_compiler_active();
// Drains and joins CPU-only work. A later request can restart the worker.
void finish_shader_compiler();
}
