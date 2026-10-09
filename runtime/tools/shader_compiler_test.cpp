#include "gfx/vulkan/shader_compiler.h"
#include <cassert>
#include <thread>
#include <stdexcept>

using namespace gfxvk::vk;
int main() {
    const std::string vertex = "#version 450\nvoid main(){gl_Position=vec4(0,0,0,1);}";
    const auto caller = uint64_t(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    auto reference = compile_shader(vertex, ShaderStage::Vertex, 1, 2, {}, false);
    auto background = compile_shader(vertex, ShaderStage::Vertex, 1, 2);
    assert(reference.error.empty() && background.error.empty());
    assert(!reference.words.empty() && reference.words == background.words);
    assert(reference.thread == caller && background.thread != caller);
    auto invalid = compile_shader("invalid GLSL", ShaderStage::Fragment, 1, 3);
    assert(invalid.words.empty() && !invalid.error.empty());
    auto compute = compile_shader("#version 450\nlayout(local_size_x=1) in;void main(){}",
                                  ShaderStage::Compute, 1, 4);
    assert(!compute.words.empty() && compute.error.empty() && compute.thread != caller);
    // Source ownership is independent of the caller's string while waiting.
    std::string owned = "#version 450\nvoid main(){vec4 p=vec4(0);";
    for (unsigned i = 0; i < 5000; ++i) owned += "p+=vec4(" + std::to_string(i) + ".0);";
    owned += "gl_Position=p;}";
    auto expected = compile_shader(owned, ShaderStage::Vertex, 1, 5, {}, false);
    auto copied = compile_shader(owned, ShaderStage::Vertex, 1, 5, [&] { owned = "invalid"; });
    assert(!expected.words.empty() && copied.words == expected.words);
    assert(!shader_compiler_active());
    finish_shader_compiler();
    auto restarted = compile_shader(vertex, ShaderStage::Vertex, 1, 2);
    assert(restarted.words == reference.words && restarted.thread != caller);
    finish_shader_compiler();
    bool rejected = false;
    try { compile_shader(std::string(4 * 1024 * 1024 + 1, ' '), ShaderStage::Vertex, 1, 2); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
}
