// SPDX-License-Identifier: MPL-2.0
// LSFG 3 compute graph adapted from SSunnKing's Enhanced fork (2f28a401).
// Only host code is included; the user supplies their own supported Lossless.dll.
#include "lsfg.h"
#include "lsfg_dll.h"
#include "deferred_dispatch.h"
#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <array>
#include <map>
#include <mutex>
#include <stdexcept>
#define VK_CHECK(expr) vk_check((expr), "LSFG: " #expr)
namespace gfxvk::fg {
void allocate_image(Image& image, VkImageView& view, uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType=VK_IMAGE_TYPE_2D; ci.format=format; ci.extent={w,h,1}; ci.mipLevels=1; ci.arrayLayers=1;
    ci.samples=VK_SAMPLE_COUNT_1_BIT; ci.tiling=VK_IMAGE_TILING_OPTIMAL; ci.usage=usage;
    ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    try {
        VK_CHECK(vkCreateImage(R.device,&ci,nullptr,&image.image));
        VkMemoryRequirements req; vkGetImageMemoryRequirements(R.device,image.image,&req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=req.size;
        ai.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(R.device,&ai,nullptr,&image.memory));
        VK_CHECK(vkBindImageMemory(R.device,image.image,image.memory,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=image.image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=format;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VK_CHECK(vkCreateImageView(R.device,&vi,nullptr,&view));
        image.width=w;image.height=h;
    } catch (...) {
        if (image.image) vkDestroyImage(R.device,image.image,nullptr);
        if (image.memory) vkFreeMemory(R.device,image.memory,nullptr);
        image={}; throw;
    }
}
namespace {
using dll::Shader;
constexpr uint32_t kShaders=49;
enum : uint32_t { S_COPY=0,S_MIPS=1,S_BLEND=2,S_QUALITY=3,S_PERF=26 };
// Roles of a network variant's shaders (offsets from S_QUALITY / S_PERF).
struct Variant {
    uint32_t feats;          // feature maps per frame and level
    uint32_t enc[4];         // encoder: luma -> half (stride 2), half -> half, half -> quarter (stride 2), -> features
    uint32_t encOut[3];      // outputs of the first three encoder layers
    uint32_t corr, corr3;    // correlation layers of heads 1/2 and of head 3
    uint32_t corrOut, corr3Out;
    uint32_t head[3][4];     // the 4 refinement layers of each head (the last writes the field)
    uint32_t headOut[3];     // outputs of each head's first three refinement layers
    uint32_t mask[5];        // UI detection: 4 layers over three frames' features, then the mask pyramid
};
const Variant kQuality = {4, {13, 14, 15, 16}, {2, 2, 4}, 3, 4, 3, 2, {{5, 6, 7, 8}, {9, 10, 11, 12}, {17, 18, 19, 20}}, {4, 4, 2},
                          {21, 22, 23, 24, 25}};
const Variant kPerf = {2, {36, 37, 38, 39}, {1, 1, 2}, 26, 27, 3, 1, {{28, 29, 30, 31}, {32, 33, 34, 35}, {40, 41, 42, 43}}, {2, 2, 1},
                       {44, 45, 46, 47, 48}};

// ---------------------------------------------------------------- state
Config g_cfg;
std::atomic<float> g_gpu_ms{0};  // the network's GPU time per game frame, smoothed (performance overlay)
bool g_loaded = false;
std::mutex g_error_mutex;
std::string g_error;  // why the last load failed

void set_error(const std::string& e) {
    std::lock_guard<std::mutex> lk(g_error_mutex);
    g_error = e;
}

Shader g_shaders[kShaders];

struct Tex {
    Image img;
    VkImageView view = VK_NULL_HANDLE;
};

struct Pipe {
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
};
Pipe g_pipes[kShaders];
VkSampler g_borderBlack = VK_NULL_HANDLE, g_borderWhite = VK_NULL_HANDLE, g_clamp = VK_NULL_HANDLE;

// a dispatch with its resources
struct Pass {
    uint32_t shader = 0;
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32_t gx = 1, gy = 1;
    uint32_t level = 0;                 // selects the uniforms
    std::vector<const Tex*> ins, outs;  // for barriers
};

constexpr uint32_t kSlots = 3;  // frames kept: copies and features
constexpr uint32_t kMaxLevels = 7, kMaxOut = 3;
constexpr VkDeviceSize kUboStride = 256;

struct Net {
    uint32_t w = 0, h = 0, levels = 0;
    float fs = 1;
    const Variant* v = nullptr;
    std::vector<std::unique_ptr<Tex>> texs;  // everything, for destruction
    Tex* dummy = nullptr;                    // unbound inputs read zero
    Tex* frames[kSlots] = {};                // copies of the input
    Tex* luma[7] = {};
    Tex* feats[kSlots][kMaxLevels][4] = {};
    Tex* out[2][kMaxOut] = {};  // generated frames, double-buffered
    std::vector<Tex*> enc[kMaxLevels][3];                 // encoder scratch (shared by the slots)
    Tex* field[kMaxLevels][3] = {};                       // per level and head: the field it produces
    std::vector<Tex*> scrA[kMaxLevels][3], scrB[kMaxLevels][3];  // and its scratch maps
    Tex* maskScr[2][2] = {};  // UI detection scratch
    Tex* mask[6] = {};        // where the image is static (the HUD): motion is suppressed there
    VkBuffer ubo = VK_NULL_HANDLE;
    Buffer uniform;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkImageView srcView = VK_NULL_HANDLE;
    std::vector<Pass> framePasses[kSlots];       // per slot written
    std::vector<Pass> genPasses[kSlots];         // per slot of the newer frame
    std::vector<Pass> blend[kSlots][2][kMaxOut];  // per slot, output parity, generated frame
    uint64_t frame = 0;                          // frames recorded since resize
    VkQueryPool queries = VK_NULL_HANDLE;        // GPU time of the network: start and end per slot
    double gpuMs = 0;
    uint32_t gpuSamples = 0;
    bool fresh = true;                           // images not yet in GENERAL layout
};
Net g;

Tex* new_tex(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags extra = 0) {
    auto t = std::make_unique<Tex>();
    allocate_image(t->img, t->view, std::max(w,1u), std::max(h,1u), fmt,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | extra);
    g.texs.push_back(std::move(t));
    return g.texs.back().get();
}

uint32_t div_up(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

VkShaderModule module_for(uint32_t i) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = g_shaders[i].spirv.size() * 4;
    ci.pCode = g_shaders[i].spirv.data();
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(R.device, &ci, nullptr, &m));
    return m;
}

VkDescriptorType type_of(uint32_t binding) {
    if (binding < 16) return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    if (binding < 32) return VK_DESCRIPTOR_TYPE_SAMPLER;
    if (binding < 48) return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

void create_pipelines() {
    std::vector<VkComputePipelineCreateInfo> cis;
    std::vector<VkShaderModule> mods;
    struct ModuleCleanup {
        std::vector<VkShaderModule>& modules;
        ~ModuleCleanup() { for (auto module : modules) vkDestroyShaderModule(R.device,module,nullptr); }
    } cleanup{mods};
    uint32_t first = g_cfg.performance ? S_PERF : S_QUALITY;
    std::vector<uint32_t> used = {S_COPY, S_MIPS, S_BLEND};
    for (uint32_t i = first; i < first + 23; i++) used.push_back(i);
    for (uint32_t i : used) {
        Pipe& p = g_pipes[i];
        std::vector<VkDescriptorSetLayoutBinding> bs;
        for (uint32_t b : g_shaders[i].bindings) bs.push_back({b, type_of(b), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.bindingCount = (uint32_t)bs.size();
        dl.pBindings = bs.data();
        VK_CHECK(vkCreateDescriptorSetLayout(R.device, &dl, nullptr, &p.dsl));
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &p.dsl;
        VK_CHECK(vkCreatePipelineLayout(R.device, &pl, nullptr, &p.layout));
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = module_for(i);
        ci.stage.pName = "main";
        ci.layout = p.layout;
        mods.push_back(ci.stage.module);
        cis.push_back(ci);
    }
    std::vector<VkPipeline> pipes(cis.size());
    const auto result=vkCreateComputePipelines(R.device, R.pipelineCache, (uint32_t)cis.size(), cis.data(), nullptr, pipes.data());
    // A failed batch can still return some live pipelines: unload() must own them before throwing.
    for (size_t k = 0; k < used.size(); k++) g_pipes[used[k]].pipe = pipes[k];
    VK_CHECK(result);

    auto sampler = [](VkSamplerAddressMode mode, VkBorderColor border) {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = mode;
        si.borderColor = border;
        si.maxLod = VK_LOD_CLAMP_NONE;
        VkSampler s;
        VK_CHECK(vkCreateSampler(R.device, &si, nullptr, &s));
        return s;
    };
    g_borderBlack = sampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
    g_borderWhite = sampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
    g_clamp = sampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
}

// a pass reading `ins` (null: unbound) and writing `outs`; `srcView` stands in for the input frame
Pass make_pass(uint32_t shader, uint32_t level, std::vector<const Tex*> ins, std::vector<const Tex*> outs, uint32_t gx,
               uint32_t gy, VkImageView srcView = VK_NULL_HANDLE) {
    Pass p;
    p.shader = shader;
    p.level = level;
    p.gx = std::max(gx, 1u);
    p.gy = std::max(gy, 1u);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = g.pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g_pipes[shader].dsl;
    VK_CHECK(vkAllocateDescriptorSets(R.device, &ai, &p.set));
    // samplers: the correlation layers sample their warped inputs with a white border
    bool white = shader == g.v->corr || shader == g.v->corr3 || shader == g.v->mask[0];
    std::vector<VkWriteDescriptorSet> ws;
    std::vector<VkDescriptorImageInfo> infos;
    infos.reserve(32);
    VkDescriptorBufferInfo ub{g.ubo, 0, 48};
    for (uint32_t b : g_shaders[shader].bindings) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = p.set;
        w.dstBinding = b;
        w.descriptorCount = 1;
        w.descriptorType = type_of(b);
        if (b < 16) {
            w.pBufferInfo = &ub;
        } else if (b < 32) {
            infos.push_back({b == 16 ? (white ? g_borderWhite : g_borderBlack) : g_clamp, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
            w.pImageInfo = &infos.back();
        } else if (b < 48) {
            uint32_t i = b - 32;
            const Tex* t = i < ins.size() ? ins[i] : nullptr;
            VkImageView v = (i == 0 && srcView) ? srcView : (t ? t->view : g.dummy->view);
            infos.push_back({VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_GENERAL});
            w.pImageInfo = &infos.back();
        } else {
            uint32_t i = b - 48;
            if (i >= outs.size() || !outs[i]) throw std::runtime_error("LSFG shader output unbound");
            infos.push_back({VK_NULL_HANDLE, outs[i]->view, VK_IMAGE_LAYOUT_GENERAL});
            w.pImageInfo = &infos.back();
        }
        ws.push_back(w);
    }
    vkUpdateDescriptorSets(R.device, (uint32_t)ws.size(), ws.data(), 0, nullptr);
    for (auto* t : ins)
        if (t) p.ins.push_back(t);
    p.outs = outs;
    return p;
}

struct LevelDims {
    uint32_t lw, lh;  // luminance
    uint32_t hw, hh;  // encoder's half size
    uint32_t fw, fh;  // features
};
LevelDims dims(uint32_t i) {
    LevelDims d;
    d.lw = std::max(1u, (uint32_t)std::floor(g.w * g.fs / (float)(1u << i)));
    d.lh = std::max(1u, (uint32_t)std::floor(g.h * g.fs / (float)(1u << i)));
    d.hw = div_up(d.lw, 2);
    d.hh = div_up(d.lh, 2);
    d.fw = div_up(d.hw, 2);
    d.fh = div_up(d.hh, 2);
    return d;
}

void write_uniforms() {
    // [frame index (0: per-frame passes, k: k-th generated frame)][level]
    auto* base = (uint8_t*)g.uniform.mapped;
    for (uint32_t k = 0; k < kMaxOut + 1; k++)
        for (uint32_t l = 0; l < kMaxLevels; l++) {
            struct {
                uint32_t offset[2], firstIter, firstIterS, advancedColorKind, hdrSupport;
                float resolutionInvScale, timestamp, uiThreshold, pad[3];
            } u{};
            u.firstIter = l == g.levels - 1;
            u.firstIterS = l == 2;
            u.resolutionInvScale = 1.0f / g.fs;
            u.timestamp = k ? (float)k / g_cfg.multiplier : 0.0f;
            u.uiThreshold = g_cfg.uiThreshold;
            memcpy(base + (k * kMaxLevels + l) * kUboStride, &u, sizeof u);
        }
}

void build_passes() {
    const Variant& v = *g.v;
    // ---- per frame: copy, luminance pyramid, encoder on each level
    for (uint32_t s = 0; s < kSlots; s++) {
        auto& P = g.framePasses[s];
        P.push_back(make_pass(S_COPY, 0, {nullptr}, {g.frames[s]}, div_up(g.w, 8), div_up(g.h, 8), g.srcView));
        LevelDims d0 = dims(0);
        P.push_back(make_pass(S_MIPS, 0, {g.frames[s]}, {g.luma[0], g.luma[1], g.luma[2], g.luma[3], g.luma[4], g.luma[5], g.luma[6]},
                              div_up(d0.lw, 64), div_up(d0.lh, 64)));
        // encoder layer by layer over all levels: the levels are independent
        std::vector<std::vector<const Tex*>> prev(g.levels);
        for (uint32_t l = 0; l < g.levels; l++) prev[l] = {g.luma[l]};
        for (uint32_t layer = 0; layer < 4; layer++)
            for (uint32_t l = 0; l < g.levels; l++) {
                LevelDims d = dims(l);
                uint32_t ow = layer < 2 ? d.hw : d.fw, oh = layer < 2 ? d.hh : d.fh;
                std::vector<const Tex*> outs;
                if (layer < 3) {
                    outs.assign(g.enc[l][layer].begin(), g.enc[l][layer].end());
                } else {
                    for (uint32_t i = 0; i < v.feats; i++) outs.push_back(g.feats[s][l][i]);
                }
                P.push_back(make_pass(v.enc[layer], l, prev[l], outs, div_up(ow, 8), div_up(oh, 8)));
                prev[l] = outs;
            }
        if (g_cfg.uiDetection) {
            // static content in the last three frames (oldest first) at the finest feature level
            LevelDims d = dims(0);
            uint32_t gx = div_up(d.fw, 8), gy = div_up(d.fh, 8);
            std::vector<const Tex*> in;
            for (uint32_t back = 2; back != ~0u; back--)
                for (uint32_t i = 0; i < v.feats; i++) in.push_back(g.feats[(s + kSlots - back) % kSlots][0][i]);
            Tex** a = g.maskScr[0];
            Tex** b = g.maskScr[1];
            P.push_back(make_pass(v.mask[0], 0, in, {a[0], a[1]}, gx, gy));
            P.push_back(make_pass(v.mask[1], 0, {a[0], a[1]}, {b[0], b[1]}, gx, gy));
            P.push_back(make_pass(v.mask[2], 0, {b[0], b[1]}, {a[0], a[1]}, gx, gy));
            P.push_back(make_pass(v.mask[3], 0, {a[0], a[1]}, {b[0], b[1]}, gx, gy));
            P.push_back(make_pass(v.mask[4], 0, {b[0], b[1]}, {g.mask[0], g.mask[1], g.mask[2], g.mask[3], g.mask[4], g.mask[5]},
                                  div_up(d.fw, 32), div_up(d.fh, 32)));
        }
    }

    // ---- per generated frame: the heads, coarse to fine; then the blend
    auto& field = g.field;
    auto& scrA = g.scrA;
    auto& scrB = g.scrB;
    for (uint32_t s = 0; s < kSlots; s++) {
        uint32_t ps = (s + kSlots - 1) % kSlots;  // the previous frame's slot
        auto& P = g.genPasses[s];
        for (int l = (int)g.levels - 1; l >= 0; l--) {
            LevelDims d = dims(l);
            uint32_t gx = div_up(d.fw, 8), gy = div_up(d.fh, 8);
            bool coarsest = l == (int)g.levels - 1;
            std::vector<const Tex*> feats;
            for (uint32_t i = 0; i < v.feats; i++) feats.push_back(g.feats[ps][l][i]);
            for (uint32_t i = 0; i < v.feats; i++) feats.push_back(g.feats[s][l][i]);
            const Tex* c1 = coarsest ? nullptr : field[l + 1][0];
            const Tex* c2 = l + 1 <= 2 ? field[l + 1][1] : nullptr;
            const Tex* c3 = l + 1 <= 2 ? field[l + 1][2] : nullptr;
            int heads = l <= 2 ? 3 : 1;
            auto take = [](const std::vector<Tex*>& t, uint32_t n) { return std::vector<const Tex*>(t.begin(), t.begin() + n); };
            // correlation
            for (int hd = 0; hd < heads; hd++) {
                std::vector<const Tex*> in = feats;
                if (hd < 2) {
                    in.push_back(hd == 0 ? c1 : c2);
                    P.push_back(make_pass(v.corr, l, in, take(scrA[l][hd], v.corrOut), gx, gy));
                } else {
                    in.push_back(c1);
                    in.push_back(c2);
                    P.push_back(make_pass(v.corr3, l, in, take(scrA[l][hd], v.corr3Out), gx, gy));
                }
            }
            // refinement: three layers ping-ponging, then the field
            for (uint32_t layer = 0; layer < 4; layer++)
                for (int hd = 0; hd < heads; hd++) {
                    uint32_t nin = layer == 0 ? (hd < 2 ? v.corrOut : v.corr3Out) : v.headOut[hd];
                    auto& src = (layer % 2 == 0) ? scrA[l][hd] : scrB[l][hd];
                    auto& dst = (layer % 2 == 0) ? scrB[l][hd] : scrA[l][hd];
                    std::vector<const Tex*> in = take(src, nin);
                    if (layer < 3) {
                        P.push_back(make_pass(v.head[hd][layer], l, in, take(dst, v.headOut[hd]), gx, gy));
                    } else {
                        in.push_back(hd == 0 ? c1 : hd == 1 ? c2 : c3);
                        if (hd < 2) in.push_back(g.mask[std::min(l, 5)]);  // null without UI detection: no mask
                        P.push_back(make_pass(v.head[hd][layer], l, in, {field[l][hd]}, gx, gy));
                    }
                }
        }
        for (uint32_t par = 0; par < 2; par++)
            for (uint32_t k = 0; k < kMaxOut; k++)
                if (g.out[par][k])
                    g.blend[s][par][k].push_back(make_pass(S_BLEND, 0, {g.frames[ps], g.frames[s], field[0][0], field[0][1], field[0][2]},
                                                           {g.out[par][k]}, div_up(g.w, 16), div_up(g.h, 16)));
    }
}

void free_net() {
    if (!g.w) return;
    for (auto& t : g.texs) {
        if (t->view) vkDestroyImageView(R.device, t->view, nullptr);
        vkDestroyImage(R.device, t->img.image, nullptr);
        vkFreeMemory(R.device, t->img.memory, nullptr);
    }
    if (g.uniform.mapped) vkUnmapMemory(R.device,g.uniform.memory);
    if (g.uniform.buffer) vkDestroyBuffer(R.device,g.uniform.buffer,nullptr);
    if (g.uniform.memory) vkFreeMemory(R.device,g.uniform.memory,nullptr);
    if (g.pool) vkDestroyDescriptorPool(R.device, g.pool, nullptr);
    if (g.queries) vkDestroyQueryPool(R.device, g.queries, nullptr);
    g = Net{};
}

// Records `passes` with barriers only where a pass reads or overwrites what an earlier one wrote
// (or overwrites what it read) since the last barrier.
void run(VkCommandBuffer cmd, const std::vector<Pass>& passes, uint32_t frameIndex, std::vector<const Tex*>& written,
         std::vector<const Tex*>& read) {
    auto has = [](const std::vector<const Tex*>& v, const Tex* t) { return std::find(v.begin(), v.end(), t) != v.end(); };
    for (const Pass& p : passes) {
        bool dep = false;
        for (auto* t : p.ins) dep |= has(written, t);
        for (auto* t : p.outs) dep |= has(written, t) || has(read, t);
        if (dep) {
            VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
            mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0,
                                 nullptr);
            written.clear();
            read.clear();
        }
        const Pipe& pp = g_pipes[p.shader];
        record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pp.pipe);
        uint32_t off = (uint32_t)((frameIndex * kMaxLevels + p.level) * kUboStride);
        record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pp.layout, 0, 1, &p.set, 1, &off);
        record::vkCmdDispatch(cmd, p.gx, p.gy, 1);
        for (auto* t : p.outs) written.push_back(t);
        for (auto* t : p.ins) read.push_back(t);
    }
}
}  // namespace

// ---------------------------------------------------------------- interface
bool load(const Config& cfg) {
    g_cfg = cfg;
    g_cfg.multiplier = std::clamp(cfg.multiplier, 2, (int)kMaxOut + 1);
    g_cfg.flowScale = std::clamp(cfg.flowScale, 0.25f, 1.0f);
    std::string err;
    // FP32 deliberately uses the upstream device feature chain unchanged.
    const bool fp16 = false;
    if (!dll::read(cfg.dll, g_shaders, err)) { set_error(err); return false; }
    bool formats=R.enabledFeatures.shaderStorageImageExtendedFormats;
    for (auto format : {VK_FORMAT_R8_UNORM,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_SFLOAT}) {
        VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(R.physicalDevice,format,&fp);
        const VkFormatFeatureFlags required=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        formats &= (fp.optimalTilingFeatures&required)==required;
    }
    if (!formats) {
        LOG("[fg] the GPU can't write r8 storage images; frame generation disabled");
        set_error("this GPU can't write the image format frame generation needs");
        return false;
    }
    create_pipelines();
    g_loaded = true;
    set_error("");

    LOG("[fg] LSFG 3 shaders loaded from %s (%s network, %s floats), x%d, flow scale %.2f, UI detection %s", cfg.dll.c_str(),
        g_cfg.performance ? "performance" : "quality", fp16 ? "16-bit" : "32-bit", g_cfg.multiplier, g_cfg.flowScale,
        g_cfg.uiDetection ? "on" : "off");
    return true;
}

std::string check_dll(const std::string& path, bool* tested) {
    std::array<Shader, kShaders> shaders;
    std::string error;
    const bool ok = dll::read(path, shaders.data(), error);
    if (tested) *tested = ok;
    return error;
}

std::string last_error() {
    std::lock_guard<std::mutex> lk(g_error_mutex);
    return g_error;
}

bool loaded() { return g_loaded; }
float gpu_ms() { return g_gpu_ms.load(std::memory_order_relaxed); }
const Config& config() { return g_cfg; }

bool resize(uint32_t w, uint32_t h, VkImageView src) {
    free_net();
    g.w = w;
    g.h = h;
    g.fs = g_cfg.flowScale;
    g.levels = h * g.fs > 512 ? 7 : 6;
    g.v = g_cfg.performance ? &kPerf : &kQuality;
    g.dummy = new_tex(1, 1, VK_FORMAT_R8G8B8A8_UNORM);
    for (auto& f : g.frames) f = new_tex(w, h, VK_FORMAT_R8G8B8A8_UNORM);
    for (uint32_t i = 0; i < 7; i++) {
        LevelDims d = dims(i);
        g.luma[i] = new_tex(d.lw, d.lh, VK_FORMAT_R8_UNORM);
    }
    for (uint32_t s = 0; s < kSlots; s++)
        for (uint32_t l = 0; l < g.levels; l++)
            for (uint32_t i = 0; i < g.v->feats; i++) g.feats[s][l][i] = new_tex(dims(l).fw, dims(l).fh, VK_FORMAT_R8G8B8A8_UNORM);
    for (uint32_t par = 0; par < 2; par++)
        for (int k = 0; k < g_cfg.multiplier - 1; k++) g.out[par][k] = new_tex(w, h, VK_FORMAT_R8G8B8A8_UNORM);
    for (uint32_t l = 0; l < g.levels; l++) {
        LevelDims d = dims(l);
        for (uint32_t layer = 0; layer < 3; layer++)
            for (uint32_t i = 0; i < g.v->encOut[layer]; i++)
                g.enc[l][layer].push_back(new_tex(layer < 2 ? d.hw : d.fw, layer < 2 ? d.hh : d.fh, VK_FORMAT_R8G8B8A8_UNORM));
        for (uint32_t hd = 0; hd < 3; hd++) {
            if (hd > 0 && l > 2) continue;  // heads 2 and 3 work on the three finest levels
            g.field[l][hd] = new_tex(d.fw, d.fh, VK_FORMAT_R16G16B16A16_SFLOAT);
            for (uint32_t i = 0; i < 4; i++) {
                g.scrA[l][hd].push_back(new_tex(d.fw, d.fh, VK_FORMAT_R8G8B8A8_UNORM));
                g.scrB[l][hd].push_back(new_tex(d.fw, d.fh, VK_FORMAT_R8G8B8A8_UNORM));
            }
        }
    }

    if (g_cfg.uiDetection) {
        LevelDims d = dims(0);
        for (auto& pair : g.maskScr)
            for (auto& t : pair) t = new_tex(d.fw, d.fh, VK_FORMAT_R8G8B8A8_UNORM);
        for (uint32_t m = 0; m < 6; m++) g.mask[m] = new_tex(d.fw >> m, d.fh >> m, VK_FORMAT_R8_UNORM);
    }

    g.uniform = create_buffer((kMaxOut + 1) * kMaxLevels * kUboStride, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    g.ubo = g.uniform.buffer;
    write_uniforms();

    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4096},
                                    {VK_DESCRIPTOR_TYPE_SAMPLER, 8192},
                                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16384},
                                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16384}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 4096;
    pi.poolSizeCount = 4;
    pi.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(R.device, &pi, nullptr, &g.pool));
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2 * kSlots;
    if (R.properties.limits.timestampComputeAndGraphics) VK_CHECK(vkCreateQueryPool(R.device, &qi, nullptr, &g.queries));
    g.srcView = src;
    build_passes();
    g.frame = 0;
    g.fresh = true;
    LOG("[fg] network for %ux%u: %u levels, features %ux%u to %ux%u, %zu images", w, h, g.levels, dims(0).fw, dims(0).fh,
        dims(g.levels - 1).fw, dims(g.levels - 1).fh, g.texs.size());
    return true;
}

std::vector<const Image*> record(VkCommandBuffer cmd, bool generate) {
    if (g.fresh) {
        std::vector<VkImageMemoryBarrier> bs;
        for (auto& t : g.texs) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = t->img.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            bs.push_back(b);
        }
        record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                             (uint32_t)bs.size(), bs.data());
        // zero: the dummy input, and the frames the UI detection reads before they exist
        VkClearColorValue zero{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (auto& t : g.texs) record::vkCmdClearColorImage(cmd, t->img.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        g.fresh = false;
    }
    // the input was just rendered; the previous frames' outputs may still be being presented
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    record::vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    uint32_t s = g.frame % kSlots, par = g.frame % 2;
    if (g.queries) {
        // the slot's previous use was two frames ago: usually done, else skip the sample
        uint64_t t[2];
        if (g.frame >= kSlots && vkGetQueryPoolResults(R.device, g.queries, 2 * s, 2, sizeof t, t, sizeof t[0],
                                                       VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            double ms = (t[1] - t[0]) * R.properties.limits.timestampPeriod * 1e-6;
            g.gpuMs += ms;
            float prev = g_gpu_ms.load(std::memory_order_relaxed);
            g_gpu_ms.store(prev ? prev + ((float)ms - prev) * 0.1f : (float)ms, std::memory_order_relaxed);
            if (++g.gpuSamples == 300) {
                LOG("[fg] network GPU time %.2f ms per game frame (%d generated)", g.gpuMs / g.gpuSamples, g_cfg.multiplier - 1);
                g.gpuMs = 0;
                g.gpuSamples = 0;
            }
        }
        record::vkCmdResetQueryPool(cmd, g.queries, 2 * s, 2);
        record::vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g.queries, 2 * s);
    }
    std::vector<const Tex*> written, read;
    run(cmd, g.framePasses[s], 0, written, read);
    std::vector<const Image*> show;
    if (g.frame > 0 && generate)
        for (int k = 0; k < g_cfg.multiplier - 1; k++) {
            // every layer warps by the timestamp: the whole estimate is per generated frame
            run(cmd, g.genPasses[s], k + 1, written, read);
            run(cmd, g.blend[s][par][k], k + 1, written, read);
            show.push_back(&g.out[par][k]->img);
        }
    show.push_back(&g.frames[s]->img);
    if (g.queries) record::vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.queries, 2 * s + 1);
    g.frame++;
    return show;
}

VkImageView view_of(const Image* img) {
    for (auto& t : g.texs)
        if (&t->img == img) return t->view;
    return VK_NULL_HANDLE;
}

void destroy() { free_net(); }

void unload() {
    free_net();
    for (Pipe& p : g_pipes) {
        if (p.pipe) vkDestroyPipeline(R.device, p.pipe, nullptr);
        if (p.layout) vkDestroyPipelineLayout(R.device, p.layout, nullptr);
        if (p.dsl) vkDestroyDescriptorSetLayout(R.device, p.dsl, nullptr);
        p = Pipe{};
    }
    for (VkSampler* s : {&g_borderBlack, &g_borderWhite, &g_clamp}) {
        if (*s) vkDestroySampler(R.device, *s, nullptr);
        *s = VK_NULL_HANDLE;
    }
    for (Shader& s : g_shaders) s = Shader{};
    g_loaded = false;
    g_gpu_ms = 0;
    set_error("");
    LOG("[fg] frame generation off");
}

}  // namespace gfxvk::fg

#undef VK_CHECK
