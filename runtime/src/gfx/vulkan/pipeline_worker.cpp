#include "pipeline_worker.h"
#include "headstart.h"
#include "pipeline_demand.h"
#include "platform/host.h"
#include "stutter_trace.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <zlib.h>

namespace gfxvk {
namespace {
std::string identity(const std::vector<uint8_t>& key) {
  return {reinterpret_cast<const char*>(key.data()), key.size()};
}
void destroy(VkDevice device, WarmPipeline& p) {
  if (p.pipeline) vkDestroyPipeline(device, p.pipeline, nullptr);
  if (p.layout) vkDestroyPipelineLayout(device, p.layout, nullptr);
  for (auto set : p.sets) if (set) vkDestroyDescriptorSetLayout(device, set, nullptr);
  p = {};
}
struct Ready { WarmPipeline handles; std::shared_ptr<PipelineRecipe> recipe; };
struct Worker {
  VkDevice device = VK_NULL_HANDLE;
  PipelineRecipeFingerprint fingerprint{};
  std::string path;
  std::string headstartPath;
  PipelineWarmupResult result;
  std::mutex mutex;
  std::thread thread;
  bool stopped = false, dirty = false;
  VkPipelineCache cache = VK_NULL_HANDLE; // Worker writes; caller reads only after join.
  bool merged = false;
  std::unordered_map<std::string, Ready> ready;
  std::unordered_map<std::string, std::shared_ptr<PipelineRecipe>> recipes;
  std::unordered_set<std::string> consumed;
  size_t recipeBytes = 0;
  ~Worker() {
    { std::lock_guard lock(mutex); stopped = true; }
    if (thread.joinable()) thread.join();
    for (auto& [key, entry] : ready) destroy(device, entry.handles);
    if (cache) vkDestroyPipelineCache(device, cache, nullptr);
  }
};
Worker& worker() { static Worker value; return value; }
size_t footprint(const PipelineRecipe& p) {
  size_t size = 8192 + p.key.size();
  for (int i = 0; i < 2; ++i)
    size += p.spirv[i].size() * 4 + p.descriptors[i].size() * sizeof(VkDescriptorSetLayoutBinding);
  return size;
}
bool insert_recipe(Worker& w, const std::shared_ptr<PipelineRecipe>& recipe) {
  auto key = identity(recipe->key);
  auto found = w.recipes.find(key);
  const size_t old = found == w.recipes.end() ? 0 : footprint(*found->second);
  const size_t size = footprint(*recipe);
  if ((found == w.recipes.end() && w.recipes.size() >= 512) ||
      size > 128 * 1024 * 1024 - (w.recipeBytes - old)) return false;
  w.recipeBytes += size - old;
  w.recipes.insert_or_assign(std::move(key), recipe);
  return true;
}
VkResult compile(VkDevice device, VkPipelineCache cache, PipelineRecipe& recipe, WarmPipeline& out) {
  VkShaderModule modules[2]{};
  auto cleanup = [&] {
    for (auto module : modules) if (module) vkDestroyShaderModule(device, module, nullptr);
  };
  VkResult result = VK_SUCCESS;
  for (int stage = 0; stage < 2 && result == VK_SUCCESS; ++stage) {
    VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor.bindingCount = recipe.descriptors[stage].size();
    descriptor.pBindings = recipe.descriptors[stage].data();
    result = vkCreateDescriptorSetLayout(device, &descriptor, nullptr, &out.sets[stage]);
    if (result != VK_SUCCESS) break;
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize = recipe.spirv[stage].size() * 4; module.pCode = recipe.spirv[stage].data();
    result = vkCreateShaderModule(device, &module, nullptr, &modules[stage]);
  }
  if (result == VK_SUCCESS) {
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 2; layout.pSetLayouts = out.sets;
    result = vkCreatePipelineLayout(device, &layout, nullptr, &out.layout);
  }
  if (result == VK_SUCCESS) {
    auto info = *recipe.state->get();
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
      stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
      stages[i].pName = "main"; stages[i].module = modules[i];
    }
    info.pStages = stages; info.layout = out.layout;
    const auto started = stutter::trace().enabled() ? stutter::now() : 0;
    result = vkCreateGraphicsPipelines(device, cache, 1, &info, nullptr, &out.pipeline);
    if (started) stutter::trace().add("startup_pipeline", 0, std::hash<std::string>{}(identity(recipe.key)),
        started, stutter::now(), int(result));
  }
  cleanup();
  if (result != VK_SUCCESS) destroy(device, out);
  else out.dynamicUniforms = recipe.dynamicUniforms;
  return result;
}
std::vector<std::shared_ptr<PipelineRecipe>> read_recipes(Worker& w, bool headstart) {
  const auto& path = headstart ? w.headstartPath : w.path;
  if (path.empty() || path == "0") return {};
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return {};
  if (size > (headstart ? 256u : 128u) * 1024 * 1024) throw std::runtime_error("recipe file size limit");
  std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), &std::fclose);
  if (!file) return {};
  if (!headstart) {
    std::vector<uint8_t> bytes(size);
    if (std::fread(bytes.data(), 1, size, file.get()) != size || std::fgetc(file.get()) != EOF || std::ferror(file.get()))
      throw std::runtime_error("recipe read failed");
    return decode_pipeline_recipes(w.fingerprint, bytes);
  }
  auto word = [](const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
  };
  std::vector<std::shared_ptr<PipelineRecipe>> result;
  size_t consumed = 0, rawBytes = 0, records = 0;
  while (consumed < size) {
    uint8_t header[12];
    if (size - consumed < 12 || std::fread(header, 1, 12, file.get()) != 12)
      throw std::runtime_error("truncated head-start header");
    consumed += 12;
    auto type = word(header), rawSize = word(header + 4), compressed = word(header + 8);
    if (++records > 100000 || type < 1 || type > 4 || !rawSize || rawSize > 16 * 1024 * 1024 ||
        !compressed || compressed > 16 * 1024 * 1024 || compressed > size - consumed ||
        rawSize > 256 * 1024 * 1024 - rawBytes) throw std::runtime_error("head-start record bounds");
    rawBytes += rawSize; consumed += compressed;
    if (type != 4) {
      if (std::fseek(file.get(), compressed, SEEK_CUR)) throw std::runtime_error("head-start seek failed");
      continue;
    }
    std::vector<uint8_t> encoded(compressed), raw(rawSize);
    if (std::fread(encoded.data(), 1, compressed, file.get()) != compressed)
      throw std::runtime_error("truncated pipeline head start");
    uLongf decoded = rawSize;
    if (uncompress(raw.data(), &decoded, encoded.data(), compressed) != Z_OK || decoded != rawSize)
      throw std::runtime_error("invalid pipeline head-start compression");
    // Different device/schema: ignore the local recipe without ever handing it
    // to Vulkan. Shader records remain useful across devices.
    if (raw.size() < 64 || std::memcmp(raw.data(), "WWVKPR01", 8))
      throw std::runtime_error("invalid pipeline head-start database");
    if (std::memcmp(raw.data() + 8, w.fingerprint.data(), w.fingerprint.size())) continue;
    auto recipes = decode_pipeline_recipes(w.fingerprint, raw);
    if (recipes.size() > 512 - result.size()) throw std::runtime_error("head-start pipeline count limit");
    result.insert(result.end(), recipes.begin(), recipes.end());
  }
  return result;
}
void run(Worker& w, std::vector<uint8_t> seed) {
  host::set_thread_name("Vulkan warmup");
  VkPipelineCache cache = VK_NULL_HANDLE;
  size_t completed = 0, failed = 0, cancelled = 0;
  try {
    std::vector<std::shared_ptr<PipelineRecipe>> loaded;
    std::unordered_set<std::string> seen;
    for (bool headstart : {false, true}) {
      try {
        for (auto& recipe : read_recipes(w, headstart))
          if (loaded.size() < 512 && seen.insert(identity(recipe->key)).second) loaded.push_back(recipe);
      } catch (const std::exception& error) {
        if (headstart) ++failed;
        std::fprintf(stderr, "[vulkan warmup] %s ignored: %s\n", headstart ? "head start" : "local recipes", error.what());
      }
    }
    if (loaded.empty()) { w.result = {0, failed}; return; }
    {
      std::lock_guard lock(w.mutex);
      for (auto& recipe : loaded)
        if (!w.consumed.contains(identity(recipe->key))) insert_recipe(w, recipe);
    }
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    ci.initialDataSize = seed.size(); ci.pInitialData = seed.data();
    if (vkCreatePipelineCache(w.device, &ci, nullptr, &cache) != VK_SUCCESS) {
      ci.initialDataSize = 0; ci.pInitialData = nullptr;
      if (vkCreatePipelineCache(w.device, &ci, nullptr, &cache) != VK_SUCCESS)
        throw std::runtime_error("worker pipeline cache creation failed");
    }
    std::fprintf(stderr, "[vulkan warmup] %zu exact pipeline recipes queued\n", loaded.size());
    for (auto& recipe : loaded) {
      // Demand has its own single compiler; stop issuing speculative work
      // while it is busy. A driver call already in flight cannot be cancelled.
      while (pipeline_demand_active()) {
        { std::lock_guard lock(w.mutex); if (w.stopped) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      auto key = identity(recipe->key);
      {
        std::lock_guard lock(w.mutex);
        if (w.stopped) break;
        if (w.consumed.contains(key)) { ++cancelled; continue; }
      }
      WarmPipeline result;
      if (compile(w.device, cache, *recipe, result) != VK_SUCCESS) { ++failed; continue; }
      bool publish;
      {
        std::lock_guard lock(w.mutex);
        publish = !w.stopped && !w.consumed.contains(key);
        if (publish) w.ready.emplace(key, Ready{result, recipe});
      }
      if (publish) ++completed;
      else { destroy(w.device, result); ++cancelled; }
    }
    std::fprintf(stderr, "[vulkan warmup] %zu ready, %zu failed, %zu cancelled\n", completed, failed, cancelled);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "[vulkan warmup] skipped: %s\n", error.what());
  }
  w.cache = cache;
  w.result = {completed, failed};
}
bool same_descriptors(const std::vector<VkDescriptorSetLayoutBinding>& a,
    const std::vector<VkDescriptorSetLayoutBinding>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].binding != b[i].binding || a[i].descriptorType != b[i].descriptorType ||
        a[i].descriptorCount != b[i].descriptorCount || a[i].stageFlags != b[i].stageFlags ||
        a[i].pImmutableSamplers || b[i].pImmutableSamplers) return false;
  return true;
}
}
void start_pipeline_worker(VkDevice device, PipelineRecipeFingerprint fingerprint,
    std::string path, std::vector<uint8_t> cacheSeed, std::string headstartPath) {
  // Construct the trace first so it outlives a worker still finishing during
  // static destruction after an exceptional shutdown.
  (void)stutter::trace();
  auto& w = worker();
  if (w.device || path.empty()) return;
  w.device = device; w.fingerprint = fingerprint; w.path = std::move(path);
  w.headstartPath = std::move(headstartPath);
  w.thread = std::thread(run, std::ref(w), std::move(cacheSeed));
}
bool take_warm_pipeline(const std::vector<uint8_t>& key,
    const std::array<std::vector<uint32_t>, 2>& spirv,
    const std::array<std::vector<VkDescriptorSetLayoutBinding>, 2>& descriptors,
    bool dynamicUniforms, WarmPipeline& out) {
  auto& w = worker();
  if (!w.device) return false;
  Ready entry;
  {
    std::lock_guard lock(w.mutex);
    auto id = identity(key);
    // This also prevents an in-flight startup job from publishing a duplicate
    // after the render thread has already compiled this key.
    if (w.consumed.size() < 4096) w.consumed.insert(id);
    auto found = w.ready.find(id);
    if (found == w.ready.end()) return false;
    entry = std::move(found->second); w.ready.erase(found);
  }
  if (entry.recipe->spirv != spirv || entry.recipe->dynamicUniforms != dynamicUniforms ||
      !same_descriptors(entry.recipe->descriptors[0], descriptors[0]) ||
      !same_descriptors(entry.recipe->descriptors[1], descriptors[1])) {
    destroy(w.device, entry.handles);
    return false;
  }
  record_headstart_pipeline(w.fingerprint, entry.recipe);
  out = entry.handles;
  return true;
}
void remember_pipeline_recipe(std::shared_ptr<PipelineRecipe> recipe) {
  auto& w = worker();
  if (!w.device) return;
  record_headstart_pipeline(w.fingerprint, recipe);
  std::lock_guard lock(w.mutex);
  if (insert_recipe(w, recipe)) w.dirty = true;
}
void save_pipeline_recipes() {
  auto& w = worker();
  if (!w.device) return;
  {
    std::lock_guard lock(w.mutex); w.stopped = true;
  }
  if (w.thread.joinable()) w.thread.join();
  std::vector<std::shared_ptr<PipelineRecipe>> snapshot;
  {
    std::lock_guard lock(w.mutex);
    if (!w.dirty) return;
    for (auto& [key, recipe] : w.recipes) snapshot.push_back(recipe);
  }
  const auto temporary = w.path + ".tmp";
  try {
    auto bytes = encode_pipeline_recipes(w.fingerprint, snapshot);
    auto parent = std::filesystem::path(w.path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) throw std::runtime_error("cannot open recipe output");
    bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    written = std::fclose(file) == 0 && written;
    if (!written || !host::replace_file(temporary, w.path)) throw std::runtime_error("recipe write failed");
    { std::lock_guard lock(w.mutex); w.dirty = false; }
    std::fprintf(stderr, "[vulkan warmup] saved %zu recipes -> %s\n", snapshot.size(), w.path.c_str());
  } catch (const std::exception& error) {
    std::error_code ec; std::filesystem::remove(temporary, ec);
    std::fprintf(stderr, "[vulkan warmup] save failed: %s\n", error.what());
  }
}
bool merge_pipeline_worker_cache(VkPipelineCache target) {
  auto& w = worker();
  if (!w.device || w.thread.joinable() || !target || !w.cache || w.merged) return false;
  const auto result = vkMergePipelineCaches(w.device, target, 1, &w.cache);
  if (result != VK_SUCCESS) {
    std::fprintf(stderr, "[vulkan warmup] cache merge failed (%d)\n", int(result));
    return false;
  }
  w.merged = true;
  std::fprintf(stderr, "[vulkan warmup] worker pipeline cache merged for persistence\n");
  return true;
}
PipelineWarmupResult finish_pipeline_warmup() {
  auto& w = worker();
  if (w.thread.joinable()) w.thread.join();
  return w.result;
}
}
