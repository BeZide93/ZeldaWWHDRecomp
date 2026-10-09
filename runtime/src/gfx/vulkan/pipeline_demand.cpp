#include "pipeline_demand.h"
#include "stutter_trace.h"
#include "platform/host.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace gfxvk {
namespace {
struct Job {
  GraphicsPipelineState state;
  uint64_t frame, key;
  const char* kind;
  std::promise<PipelineBuildResult> done;
};
struct Compiler {
  VkDevice device = VK_NULL_HANDLE;
  VkPipelineCache cache = VK_NULL_HANDLE;
  std::vector<uint8_t> seed;
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::shared_ptr<Job>> queue;
  std::thread thread;
  std::atomic<unsigned> active{0};
  bool stopped = false, changed = false;
  ~Compiler() {
    { std::lock_guard lock(mutex); stopped = true; }
    wake.notify_all();
    if (thread.joinable()) thread.join();
    if (cache) vkDestroyPipelineCache(device, cache, nullptr);
  }
};
Compiler& compiler() { static Compiler value; return value; }
void run(Compiler& c) {
  host::set_thread_name("Vulkan demand");
  // Only this thread ever creates/modifies this cache. The main thread reads
  // or merges it only after joining us under the renderer execution lock.
  VkResult cacheResult = VK_SUCCESS;
  if (!c.cache) {
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = c.seed.size(); info.pInitialData = c.seed.data();
    cacheResult = vkCreatePipelineCache(c.device, &info, nullptr, &c.cache);
    if (cacheResult != VK_SUCCESS && !c.seed.empty()) {
      info.initialDataSize = 0; info.pInitialData = nullptr;
      cacheResult = vkCreatePipelineCache(c.device, &info, nullptr, &c.cache);
    }
    c.seed.clear();
  }
  for (;;) {
    std::shared_ptr<Job> job;
    {
      std::unique_lock lock(c.mutex);
      c.wake.wait(lock, [&] { return c.stopped || !c.queue.empty(); });
      if (c.queue.empty()) return;
      job = std::move(c.queue.front()); c.queue.pop_front();
    }
    try {
      PipelineBuildResult result;
      const auto started = stutter::now();
      result.result = cacheResult == VK_SUCCESS
          ? vkCreateGraphicsPipelines(c.device, c.cache, 1, job->state.get(), nullptr, &result.pipeline)
          : cacheResult;
      const auto ended = stutter::now();
      result.compileNs = ended - started;
      if (stutter::trace().enabled()) stutter::trace().add(job->kind, job->frame, job->key, started, ended, int(result.result));
      if (result.result != VK_SUCCESS && result.pipeline) {
        vkDestroyPipeline(c.device, result.pipeline, nullptr);
        result.pipeline = VK_NULL_HANDLE;
      }
      c.changed |= result.result == VK_SUCCESS;
      job->done.set_value(result);
    } catch (...) {
      job->done.set_exception(std::current_exception());
    }
    c.active.fetch_sub(1, std::memory_order_release);
  }
}
}
void start_pipeline_demand(VkDevice device, std::vector<uint8_t> seed) {
  (void)stutter::trace(); // Trace must outlive compiler static destruction.
  auto& c = compiler();
  if (c.device && c.device != device) throw std::runtime_error("pipeline compiler device changed without teardown");
  if (c.device) return;
  c.device = device; c.seed = std::move(seed);
  try { c.thread = std::thread(run, std::ref(c)); }
  catch (...) { c.device = VK_NULL_HANDLE; throw; }
}
PipelineBuildResult build_pipeline_async(const GraphicsPipelineState& state,
    uint64_t frame, uint64_t key, const char* kind,
    const std::function<void()>& presentWhilePending) {
  auto& c = compiler();
  if (!c.device) throw std::runtime_error("pipeline demand compiler unavailable");
  auto job = std::make_shared<Job>(Job{state, frame, key, kind, {}});
  auto done = job->done.get_future();
  const auto requested = stutter::trace().enabled() ? stutter::now() : 0;
  {
    std::lock_guard lock(c.mutex);
    if (c.stopped) {
      if (c.thread.joinable()) throw std::runtime_error("pipeline compiler shutdown in progress");
      c.stopped = false; c.thread = std::thread(run, std::ref(c));
    }
    // Normal rendering has one ordered caller and one outstanding job. Bound
    // misuse as well, rather than silently dropping a draw under pressure.
    if (c.queue.size() >= 8) throw std::runtime_error("pipeline demand queue capacity exceeded");
    c.queue.push_back(job); c.active.fetch_add(1, std::memory_order_release);
    if (requested) stutter::trace().add("pipeline_request", frame, key, requested, requested, int(c.queue.size()));
  }
  c.wake.notify_one();
  std::exception_ptr presentationError;
  while (done.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
    if (!presentationError && presentWhilePending) try { presentWhilePending(); }
    catch (...) { presentationError = std::current_exception(); }
  }
  auto result = done.get();
  if (requested) stutter::trace().add("pipeline_pending", frame, key, requested, stutter::now(), int(result.result));
  if (presentationError) {
    if (result.pipeline) vkDestroyPipeline(c.device, result.pipeline, nullptr);
    std::rethrow_exception(presentationError);
  }
  return result;
}
bool pipeline_demand_active() { return compiler().active.load(std::memory_order_acquire) != 0; }
bool async_pipeline_enabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWHD_VK_ASYNC_PIPELINES");
    return !value || std::strcmp(value, "0");
  }();
  return enabled;
}
bool finish_pipeline_demand(VkPipelineCache target) {
  auto& c = compiler();
  if (!c.device) return false;
  { std::lock_guard lock(c.mutex); c.stopped = true; }
  c.wake.notify_all();
  if (c.thread.joinable()) c.thread.join();
  if (!target || !c.cache || !c.changed) return false;
  const auto result = vkMergePipelineCaches(c.device, target, 1, &c.cache);
  if (result != VK_SUCCESS) return false;
  c.changed = false;
  return true;
}
}
