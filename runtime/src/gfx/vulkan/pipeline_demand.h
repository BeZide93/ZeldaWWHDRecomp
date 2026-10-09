#pragma once
#include "pipeline_state.h"
#include <functional>

namespace gfxvk {
struct PipelineBuildResult {
  VkResult result = VK_ERROR_INITIALIZATION_FAILED;
  VkPipeline pipeline = VK_NULL_HANDLE;
  uint64_t compileNs = 0;
};
// A dedicated compiler owns its cache. The caller retains the immutable Vulkan
// modules/layout until this operation returns; all CPU create-info arrays are
// copied into the queued job. The callback services completed-frame presentation
// while the exact draw stays at the head of the command stream.
void start_pipeline_demand(VkDevice device, std::vector<uint8_t> seed);
bool async_pipeline_enabled();
PipelineBuildResult build_pipeline_async(const GraphicsPipelineState& state,
    uint64_t frame, uint64_t key, const char* kind,
    const std::function<void()>& presentWhilePending);
bool pipeline_demand_active();
// Under the renderer execution lock, after queued rendering has drained.
bool finish_pipeline_demand(VkPipelineCache target);
}
