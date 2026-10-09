#include "gfx/vulkan/pipeline_demand.h"
#include "pipeline_state_fixture.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>

using namespace gfxvk;
namespace {
template<class T> T handle(uintptr_t value) {
  if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
  else return T(value);
}
const auto caller = std::this_thread::get_id();
std::mutex gateMutex;
std::condition_variable gate;
bool released = false;
VkResult desired = VK_SUCCESS;
std::atomic<unsigned> creates = 0, destroys = 0, pipelinesDestroyed = 0, merges = 0;
VKAPI_ATTR VkResult VKAPI_CALL create_cache(VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache* out) {
  assert(std::this_thread::get_id() != caller);
  *out = handle<VkPipelineCache>(10); ++creates; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy_cache(VkDevice, VkPipelineCache, const VkAllocationCallbacks*) { ++destroys; }
VKAPI_ATTR void VKAPI_CALL destroy_pipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*) { ++pipelinesDestroyed; }
VKAPI_ATTR VkResult VKAPI_CALL merge_cache(VkDevice, VkPipelineCache, uint32_t count, const VkPipelineCache*) {
  assert(std::this_thread::get_id() == caller && count == 1);
  ++merges; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL create_pipeline(VkDevice, VkPipelineCache, uint32_t count,
    const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks*, VkPipeline* out) {
  assert(std::this_thread::get_id() != caller && count == 1);
  std::unique_lock lock(gateMutex);
  gate.wait(lock, [] { return released; });
  // The callback mutated the source snapshot during compilation. This job
  // must still see the original CPU arrays and retained immutable handles.
  assert(info->pVertexInputState->pVertexBindingDescriptions[0].stride == 24);
  assert(info->layout == handle<VkPipelineLayout>(7));
  assert(info->pStages[0].module == handle<VkShaderModule>(8));
  assert(info->pStages[1].module == handle<VkShaderModule>(9));
  *out = handle<VkPipeline>(42); // Deliberately return a partial handle on failure.
  return desired;
}
void reset(VkResult result) {
  std::lock_guard lock(gateMutex); released = false; desired = result;
}
void release() {
  { std::lock_guard lock(gateMutex); released = true; }
  gate.notify_all();
}
GraphicsPipelineState state() {
  auto result = pipeline_test::state();
  auto* info = const_cast<VkGraphicsPipelineCreateInfo*>(result.get());
  info->layout = handle<VkPipelineLayout>(7);
  auto* stages = const_cast<VkPipelineShaderStageCreateInfo*>(info->pStages);
  stages[0].module = handle<VkShaderModule>(8); stages[1].module = handle<VkShaderModule>(9);
  return result;
}
}
namespace gfxvk {
PFN_vkCreatePipelineCache vkCreatePipelineCache = create_cache;
PFN_vkDestroyPipelineCache vkDestroyPipelineCache = destroy_cache;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = create_pipeline;
PFN_vkDestroyPipeline vkDestroyPipeline = destroy_pipeline;
PFN_vkMergePipelineCaches vkMergePipelineCaches = merge_cache;
}
int main() {
  std::atexit([] { assert(creates == destroys); });
  start_pipeline_demand(handle<VkDevice>(1), {});
  for (auto result : {VK_SUCCESS, VK_ERROR_UNKNOWN}) {
    reset(result);
    auto source = state();
    unsigned pumps = 0;
    auto built = build_pipeline_async(source, 3, 4, "guest_pipeline", [&] {
      auto* info = source.get();
      const_cast<VkVertexInputBindingDescription*>(info->pVertexInputState->pVertexBindingDescriptions)[0].stride = 999;
      if (++pumps == 3) release();
    });
    assert(pumps >= 3 && built.result == result);
    if (result == VK_SUCCESS) {
      assert(built.pipeline == handle<VkPipeline>(42));
      vkDestroyPipeline(handle<VkDevice>(1), built.pipeline, nullptr);
    } else assert(!built.pipeline);
  }
  assert(pipelinesDestroyed == 2); // Caller success + worker partial-failure cleanup.
  reset(VK_SUCCESS);
  bool propagated = false;
  try {
    build_pipeline_async(state(), 3, 4, "guest_pipeline", [&] {
      release(); throw std::runtime_error("presentation failure");
    });
  } catch (const std::runtime_error&) { propagated = true; }
  assert(propagated && pipelinesDestroyed == 3);
  assert(finish_pipeline_demand(handle<VkPipelineCache>(11)));
  assert(!pipeline_demand_active() && merges == 1 && creates == 1);
  // A cache checkpoint may be followed by more rendering. Restart safely,
  // retaining the compiler cache rather than creating/leaking another one.
  reset(VK_SUCCESS);
  auto built = build_pipeline_async(state(), 3, 4, "guest_pipeline", release);
  assert(built.result == VK_SUCCESS);
  vkDestroyPipeline(handle<VkDevice>(1), built.pipeline, nullptr);
  assert(finish_pipeline_demand(handle<VkPipelineCache>(11)));
  assert(merges == 2 && creates == 1);
  // Utility bootstrap has no completed image to pump. An absent callback is
  // valid and must not turn a slow successful compilation into an exception.
  reset(VK_SUCCESS);
  std::thread unblock([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    release();
  });
  auto utility = build_pipeline_async(state(), 3, 0, "present_pipeline", {});
  unblock.join();
  assert(utility.result == VK_SUCCESS && utility.pipeline);
  vkDestroyPipeline(handle<VkDevice>(1), utility.pipeline, nullptr);
  assert(finish_pipeline_demand(handle<VkPipelineCache>(11)));
  assert(merges == 3 && creates == 1);
}
