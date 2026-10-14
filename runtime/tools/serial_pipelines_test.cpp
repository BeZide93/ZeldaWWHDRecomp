#include "gfx/vulkan/loader.h"
#include "gfx/vulkan/serial_pipelines.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

namespace {
std::atomic<int> active{}, maximum{}, calls{}, renderingLookups{};
VkResult work() {
  int n = ++active;
  int old = maximum.load();
  while (old < n && !maximum.compare_exchange_weak(old, n)) {}
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ++calls;
  --active;
  return VK_PIPELINE_COMPILE_REQUIRED;
}
VKAPI_ATTR VkResult VKAPI_CALL graphics(VkDevice, VkPipelineCache, uint32_t,
    const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*) { return work(); }
VKAPI_ATTR VkResult VKAPI_CALL compute(VkDevice, VkPipelineCache, uint32_t,
    const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*) { return work(); }
VKAPI_ATTR VkResult VKAPI_CALL cache(VkDevice, VkPipelineCache, size_t*, void*) { return work(); }
VKAPI_ATTR VkResult VKAPI_CALL merge(VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*) { return work(); }
VKAPI_ATTR void VKAPI_CALL unused() {}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkDevice, const char* name) {
  if (std::strstr(name,"vkCmdBeginRendering")==name || std::strstr(name,"vkCmdEndRendering")==name)++renderingLookups;
  if (!std::strcmp(name, "vkCreateGraphicsPipelines")) return reinterpret_cast<PFN_vkVoidFunction>(graphics);
  if (!std::strcmp(name, "vkCreateComputePipelines")) return reinterpret_cast<PFN_vkVoidFunction>(compute);
  if (!std::strcmp(name, "vkGetPipelineCacheData")) return reinterpret_cast<PFN_vkVoidFunction>(cache);
  if (!std::strcmp(name, "vkMergePipelineCaches")) return reinterpret_cast<PFN_vkVoidFunction>(merge);
  return unused;
}
}
int main() {
  using namespace gfxvk;
  for (unsigned release : {0, 502, 699, 700, 762}) {
    auto v = VK_MAKE_VERSION(1, release, 42);
    assert(serial_pipelines_wanted(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, v, nullptr) == (release < 700));
    assert(!serial_pipelines_wanted(VK_DRIVER_ID_MESA_TURNIP, v, nullptr));
    assert(!serial_pipelines_wanted(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, v, "0"));
    assert(serial_pipelines_wanted(VK_DRIVER_ID_MESA_TURNIP, v, "1"));
  }
  assert(!serial_pipelines_wanted(VK_DRIVER_ID_NVIDIA_PROPRIETARY, 0, nullptr));
  assert(!serial_pipelines_wanted(VkDriverId(0), 0, nullptr));
  assert(!serial_pipelines_wanted(VK_DRIVER_ID_MESA_TURNIP, 0, "auto"));
  vkGetDeviceProcAddr = get_proc;
  // Reinitialization must not retain a wrapper or the previous device's setting.
  for (int round = 0; round < 4; ++round) {
    // The first load deliberately takes the classic early return: its raw
    // pipeline/cache entry points must still be captured before wrappers start.
    const bool classic=(round%2)==0;
    renderingLookups=0;
    load_device_functions(VK_NULL_HANDLE, round>=2, classic);
    assert(renderingLookups==(classic?0:2));
    assert((vkCmdBeginRendering==nullptr)==classic);
    assert((vkCmdEndRendering==nullptr)==classic);
    assert(vkCreateGraphicsPipelines == graphics);
    serialize_pipeline_creation(true);
    active = maximum = calls = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([i] {
      for (int j = 0; j < 4; ++j) {
        VkResult result;
        switch (i % 4) {
          case 0: result = vkCreateGraphicsPipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, nullptr, nullptr, nullptr); break;
          case 1: result = vkCreateComputePipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, nullptr, nullptr, nullptr); break;
          case 2: result = vkGetPipelineCacheData(VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, nullptr); break;
          default: result = vkMergePipelineCaches(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, nullptr); break;
        }
        assert(result == VK_PIPELINE_COMPILE_REQUIRED);
      }
    });
    for (auto& t : threads) t.join();
    assert(maximum == 1 && calls == 32);
    serialize_pipeline_creation(false);
    assert(vkCreateGraphicsPipelines == graphics);
    assert(vkCreateComputePipelines == compute);
    assert(vkGetPipelineCacheData == cache);
    assert(vkMergePipelineCaches == merge);
  }
}
