// Offline replay of backend-neutral GX2 shader records from shaderprep.py.
// Metal pipeline recipes (type 2) are intentionally never interpreted here.
#include "shaders.h"
#include "headstart.h"
#include "pipeline_worker.h"
#include "backend.h"
#include "runtime.h"
#include "ppc.h"
#include "platform/host.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include <zlib.h>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace gfxvk {
namespace {
struct Recording {
  const char* path = std::getenv("WWHD_VK_RECIPE_RECORD");
  std::mutex mutex;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> records;
  size_t bytes = 0, dropped = 0;
  bool stopped = false;
};
Recording& recording() { static Recording value; return value; }
void append_word(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (8 * i)));
}
uint32_t word(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
struct ProgramPool {
  std::unordered_map<std::string, uint32_t> addresses;
  size_t bytes = 0;
  uint32_t copy(const uint8_t* data, uint32_t size) {
    std::string key(reinterpret_cast<const char*>(data), size);
    auto found = addresses.find(key);
    if (found != addresses.end()) return found->second;
    if (size > 128 * 1024 * 1024 - bytes)
      throw std::runtime_error("head-start guest program pool exceeds 128 MiB");
    uint32_t address = mem::host_alloc(size, 256);
    if (!address) throw std::runtime_error("head-start guest allocation failed");
    std::memcpy(mem::ptr(address), data, size);
    bytes += size;
    addresses.emplace(std::move(key), address);
    return address;
  }
};
}

void record_headstart_shader(const uint32_t* regs, bool vertex) {
  auto& recorder = recording();
  if (!recorder.path || !*recorder.path || !std::strcmp(recorder.path, "0")) return;
  const uint32_t start = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
  const uint32_t address = regs[start] << 8, size = regs[start + 1] << 3;
  const uint32_t fetchAddress = regs[mmSQ_PGM_START_FS] << 8;
  if (!size || size > 1024 * 1024 || uint64_t(address) + size > 0x100000000ull) return;
  uint32_t fetchSize = 0;
  if (fetchAddress) {
    if (ld32(fetchAddress) == 0x57574653) {
      uint32_t count = ld32(fetchAddress + 4);
      if (count > 64) return;
      fetchSize = 16 + count * 16;
    } else fetchSize = regs[mmSQ_PGM_START_FS + 1] << 3;
    if (!fetchSize || fetchSize > 4096 || uint64_t(fetchAddress) + fetchSize > 0x100000000ull) return;
  }
  std::vector<uint8_t> raw;
  uint32_t count = 0;
  for (uint32_t i = 0; i < 0x10000; ++i) count += regs[i] != 0;
  raw.reserve(16 + size + fetchSize + count * 8);
  append_word(raw, vertex); append_word(raw, size); append_word(raw, fetchSize); append_word(raw, count);
  raw.insert(raw.end(), mem::ptr(address), mem::ptr(address) + size);
  if (fetchSize) raw.insert(raw.end(), mem::ptr(fetchAddress), mem::ptr(fetchAddress) + fetchSize);
  for (uint32_t i = 0; i < 0x10000; ++i)
    if (regs[i]) { append_word(raw, i); append_word(raw, regs[i]); }
  std::lock_guard lock(recorder.mutex);
  if (recorder.stopped) return;
  if (raw.size() > 128 * 1024 * 1024 - recorder.bytes) { ++recorder.dropped; return; }
  recorder.bytes += raw.size();
  recorder.records.emplace_back(1, std::move(raw));
}

void record_headstart_pipeline(const std::array<uint8_t, 48>& fingerprint,
    const std::shared_ptr<PipelineRecipe>& recipe) {
  auto& recorder = recording();
  if (!recorder.path || !*recorder.path || !std::strcmp(recorder.path, "0")) return;
  try {
    auto raw = encode_pipeline_recipes(fingerprint, {recipe});
    std::lock_guard lock(recorder.mutex);
    if (recorder.stopped) return;
    if (raw.size() > 128 * 1024 * 1024 - recorder.bytes) { ++recorder.dropped; return; }
    recorder.bytes += raw.size();
    recorder.records.emplace_back(4, std::move(raw));
  } catch (const std::exception& error) {
    std::fprintf(stderr, "[vulkan headstart] pipeline recording skipped: %s\n", error.what());
  }
}

// Thread-safe freeze: called at the scripted endpoint or under the renderer's
// orderly shutdown lock. Compression and writes happen after recording stops.
void save_headstart_recording() {
  save_pipeline_recipes();
  auto& recorder = recording();
  if (!recorder.path || !*recorder.path || !std::strcmp(recorder.path, "0")) return;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> records;
  size_t dropped;
  {
    std::lock_guard lock(recorder.mutex);
    if (recorder.stopped) return;
    recorder.stopped = true;
    records.swap(recorder.records);
    dropped = recorder.dropped;
  }
  std::string temporary = std::string(recorder.path) + ".tmp";
  try {
    auto parent = std::filesystem::path(recorder.path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(temporary.c_str(), "wb"), &std::fclose);
    if (!file) throw std::runtime_error("cannot open recording output");
    for (const auto& [type, raw] : records) {
      uLongf size = compressBound(raw.size());
      std::vector<uint8_t> compressed(size), header;
      if (compress2(compressed.data(), &size, raw.data(), raw.size(), 6) != Z_OK)
        throw std::runtime_error("recording compression failed");
      append_word(header, type); append_word(header, raw.size()); append_word(header, size);
      if (std::fwrite(header.data(), 1, header.size(), file.get()) != header.size() ||
          std::fwrite(compressed.data(), 1, size, file.get()) != size)
        throw std::runtime_error("recording write failed");
    }
    if (std::fclose(file.release())) throw std::runtime_error("recording close failed");
    if (!host::replace_file(temporary, recorder.path)) throw std::runtime_error("recording replacement failed");
    std::fprintf(stderr, "[vulkan headstart] recorded %zu shader/pipeline records, %zu dropped -> %s\n",
        records.size(), dropped, recorder.path);
  } catch (const std::exception& error) {
    std::error_code ec; std::filesystem::remove(temporary, ec);
    std::fprintf(stderr, "[vulkan headstart] recording failed: %s\n", error.what());
  }
}

int warm_headstart_shaders() {
  const char* override = std::getenv("WWHD_HEADSTART");
  const std::string path = override ? override : config::game_dir + "/shadercache/headstart.bin";
  if (path == "0") {
    std::fprintf(stderr, "[vulkan headstart] disabled by WWHD_HEADSTART=0\n");
    return 1;
  }
  try {
    if (std::filesystem::file_size(path) > 256 * 1024 * 1024)
      throw std::runtime_error("head-start file exceeds 256 MiB");
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), &std::fclose);
    if (!file) throw std::runtime_error("cannot open head-start file");
    ProgramPool pool;
    std::array<uint32_t, 0x10000> regs{};
    size_t known = 0, speculative = 0, failed = 0, metalPipelines = 0, vulkanPipelines = 0, count = 0, rawBytes = 0;
    std::array<uint8_t, 12> header;
    for (;;) {
      size_t got = std::fread(header.data(), 1, header.size(), file.get());
      if (!got && !std::ferror(file.get())) break;
      if (got != header.size()) throw std::runtime_error("truncated record header");
      uint32_t type = word(header.data()), size = word(header.data() + 4), compressed = word(header.data() + 8);
      if (++count > 100000 || !size || size > 16 * 1024 * 1024 ||
          !compressed || compressed > 16 * 1024 * 1024 || size > 256 * 1024 * 1024 - rawBytes)
        throw std::runtime_error("invalid head-start record bounds");
      rawBytes += size;
      std::vector<uint8_t> encoded(compressed), raw(size);
      if (std::fread(encoded.data(), 1, encoded.size(), file.get()) != encoded.size())
        throw std::runtime_error("truncated record payload");
      uLongf decoded = size;
      if (uncompress(raw.data(), &decoded, encoded.data(), encoded.size()) != Z_OK || decoded != size)
        throw std::runtime_error("invalid head-start compressed payload");
      if (type == 2) { ++metalPipelines; continue; }
      if (type == 4) { ++vulkanPipelines; continue; } // Owned startup worker validates/compiles these.
      if (type != 1 && type != 3) throw std::runtime_error("unknown head-start record type");
      if (size < 16) throw std::runtime_error("truncated shader record");
      uint32_t vertex = word(raw.data()), codeSize = word(raw.data() + 4);
      uint32_t fetchSize = word(raw.data() + 8), nregs = word(raw.data() + 12);
      if (vertex > 1 || !codeSize || codeSize % 8 || fetchSize > 4096 || fetchSize % 8 || nregs > regs.size() ||
          16ull + codeSize + fetchSize + uint64_t(nregs) * 8 != size)
        throw std::runtime_error("invalid shader record bounds");
      regs.fill(0);
      const uint8_t* values = raw.data() + 16 + codeSize + fetchSize;
      for (uint32_t i = 0; i < nregs; ++i) {
        uint32_t index = word(values + i * 8);
        if (index >= regs.size()) throw std::runtime_error("invalid shader register index");
        regs[index] = word(values + i * 8 + 4);
      }
      uint32_t start = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
      regs[start] = pool.copy(raw.data() + 16, codeSize) >> 8;
      regs[start + 1] = codeSize >> 3;
      regs[mmSQ_PGM_START_FS] = fetchSize ? pool.copy(raw.data() + 16 + codeSize, fetchSize) >> 8 : 0;
      regs[mmSQ_PGM_START_FS + 1] = fetchSize >> 3;
      uint64_t fetchKey = 0;
      auto* fetch = vk::get_fetch_shader(regs.data(), &fetchKey);
      if (vertex && !fetch) { ++failed; continue; }
      auto* shader = vk::translate(regs.data(), vertex != 0, fetch, fetchKey);
      if (!shader || !shader->ready()) ++failed;
      else if (type == 1) ++known;
      else ++speculative;
    }
    const auto pipelines = finish_pipeline_warmup();
    save_pipeline_cache();
    const bool dirty = vk::shader_cache_dirty();
    bool saved = vk::save_shader_cache();
    auto stats = vk::shader_stats();
    std::fprintf(stderr, "[vulkan headstart] known %zu speculative %zu failed %zu; skipped %zu Metal pipeline recipes; SPIR-V compiles %llu disk hits %llu saves %llu; cache checkpoint %s\n",
        known, speculative, failed, metalPipelines, (unsigned long long)stats.spirvCompiles,
        (unsigned long long)stats.diskHits, (unsigned long long)stats.diskSaves,
        !dirty ? "unchanged" : saved ? "ok" : "failed");
    std::fprintf(stderr, "[vulkan headstart] %zu Vulkan pipeline records; %zu ready, %zu failed\n",
        vulkanPipelines, pipelines.ready, pipelines.failed);
    return failed || pipelines.failed || (dirty && !saved) || !(known + speculative + pipelines.ready) ? 1 : 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "[vulkan headstart] %s: %s\n", path.c_str(), error.what());
    return 1;
  }
}
}
