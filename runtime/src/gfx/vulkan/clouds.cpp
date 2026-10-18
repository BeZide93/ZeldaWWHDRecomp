// Sky passes of the cloud work on Vulkan (cloud_proto.cpp has the game side, gfx/metal_clouds.mm the
// Metal side). For now: the horizon-haze grade (Graphics > Horizon haze), drawn into the half-resolution
// sky buffer when the game starts drawing its sky clouds, after the sky dome and its haze band.
#include "backend.h"
#include "deferred_dispatch.h"
#include "pass.h"
#include "shaders.h"
#include "../../cloud_proto.h"
#include "../../exception_report.h"
#include "gfx/renderer.h"
#include "runtime.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

namespace gfxvk {
namespace {
void check_vk(VkResult result, const char* what) {
    if (result != VK_SUCCESS) exception_report::raise(std::string(what) + " failed (" + std::to_string(int(result)) + ")");
}
VkShaderModule sky_module(const char* source, bool vertex) {
    std::string error;
    auto words = vk::compile_glsl(source, vertex, &error);
    if (words.empty() || !error.empty()) exception_report::raise("Vulkan sky shader: " + error);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * 4;
    ci.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check_vk(vkCreateShaderModule(R.device, &ci, nullptr, &module), "create sky shader");
    return module;
}

const char* kSkyVertex = R"glsl(#version 450
layout(location=0) out vec2 ndc;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    ndc = vec2(p.x, -p.y);  // Vulkan's y points down; the shader works in GL-style NDC (y up)
    gl_Position = vec4(p, 0.5, 1.0);
}
)glsl";
// the same grade as metal_clouds.mm grade_fs: pull the horizon band back toward the sky colour, keeping
// a warm glow around a low sun
const char* kGradeFragment = R"glsl(#version 450
layout(push_constant) uniform Sky {
    vec4 view0, view1, view2, proj, sun, lit, sky, params;
} g;
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 color;
void main() {
    mat3 view = mat3(g.view0.xyz, g.view1.xyz, g.view2.xyz);
    vec3 dc = normalize(vec3(ndc.x / g.proj.x, ndc.y / g.proj.y, -1.0));
    vec3 rd = transpose(view) * dc;
    float band = (1.0 - smoothstep(0.0, 0.32, rd.y)) * step(-0.02, rd.y);
    vec3 skyc = g.sky.rgb * mix(0.95, 1.12, clamp(rd.y * 4.0, 0.0, 1.0));
    float aroundSun = pow(clamp(dot(rd, g.sun.xyz), 0.0, 1.0), 6.0) * g.lit.w;
    float a = clamp(g.params.z, 0.0, 1.0) * band * (1.0 - aroundSun);
    color = vec4(skyc * a, a);
}
)glsl";

struct Resources {
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkPipeline> pipelines;  // by the pass's attachment formats
} res;

VkPipeline grade_pipeline() {
    if (res.device && res.device != R.device) res = {};
    res.device = R.device;
    if (!res.layout) {
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cloud_proto::SkyFrame)};
        VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &push;
        check_vk(vkCreatePipelineLayout(R.device, &ci, nullptr, &res.layout), "create sky grade layout");
    }
    std::string key = "grade";
    uint32_t ncolor = 0;
    VkFormat colorFormats[8]{};
    for (uint32_t i = 0; i < 8; i++)
        if (R.passColors[i]) { colorFormats[i] = R.passColors[i]->fmt.pixel; ncolor = i + 1; }
    for (uint32_t i = 0; i < ncolor; i++) key += "/" + std::to_string(int(colorFormats[i]));
    if (R.passDepth) key += "/d" + std::to_string(int(R.passDepth->fmt.pixel)) + (R.passDepth->fmt.stencil ? "s" : "");
    if (auto it = res.pipelines.find(key); it != res.pipelines.end()) return it->second;
    VkShaderModule vs = sky_module(kSkyVertex, true), fs = sky_module(kGradeFragment, false);
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? fs : vs;
        stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend[8]{};
    for (uint32_t i = 0; i < ncolor; i++) blend[i].colorWriteMask = 0;
    blend[0].blendEnable = VK_TRUE;  // premultiplied: rgb + dst * (1 - a); destination alpha kept
    blend[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend[0].colorBlendOp = VK_BLEND_OP_ADD;
    blend[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].alphaBlendOp = VK_BLEND_OP_ADD;
    blend[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = ncolor;
    bs.pAttachments = blend;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = ncolor;
    rendering.pColorAttachmentFormats = colorFormats;
    if (R.passDepth) {
        rendering.depthAttachmentFormat = R.passDepth->fmt.pixel;
        rendering.stencilAttachmentFormat = R.passDepth->fmt.stencil ? R.passDepth->fmt.pixel : VK_FORMAT_UNDEFINED;
    }
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_targets(ci, rendering);
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &dss;
    ci.pColorBlendState = &bs;
    ci.pDynamicState = &ds;
    ci.layout = res.layout;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe);
    vkDestroyShaderModule(R.device, vs, nullptr);
    vkDestroyShaderModule(R.device, fs, nullptr);
    if (r != VK_SUCCESS) {
        LOG("[sky] Vulkan grade pipeline %s failed (%d)", key.c_str(), int(r));
        pipe = VK_NULL_HANDLE;
    } else {
        LOG("[sky] Vulkan grade pipeline %s", key.c_str());
    }
    res.pipelines.emplace(key, pipe);
    return pipe;
}

}  // namespace

// OP_CLOUDS_BEGIN (renderer table): the sky buffer's pass is the open one
static uint32_t g_skyAddr = 0, g_skySeq = 0;
static uint64_t g_skyFrame = ~0ull;
void sky_clouds_begin(const uint32_t* regs, uint32_t seq) {
    (void)regs;
    static uint64_t begins = 0, open = 0;
    begins++;
    if (R.rendering && R.passColors[0]) g_skyAddr = R.passColors[0]->addr, g_skySeq = seq, g_skyFrame = R.frame, open++;
    if (begins % 300 == 0 && render::cloud_shadows())
        LOG("[shadows] Vulkan: sky begins %llu, with an open pass %llu", (unsigned long long)begins, (unsigned long long)open);
    const float reduce = 1.0f - render::horizon_haze();
    if (reduce <= 0.0f) return;
    static uint64_t missed = 0;
    if (!R.rendering || !R.passTracked || !R.passColors[0] || !R.passWidth || !R.passHeight) {
        if (missed++ % 600 == 0) LOG("[sky] horizon haze: no open sky pass (%llu times)", (unsigned long long)missed);
        return;
    }
    VkPipeline pipe = grade_pipeline();
    if (!pipe) return;
    cloud_proto::SkyFrame g;
    cloud_proto::sky_frame(*cloud_proto::snapshot(seq), float(R.passWidth) / float(R.passHeight), reduce, g);
    VkCommandBuffer cmd = command_buffer();
    VkViewport viewport{0, 0, float(R.passWidth), float(R.passHeight), 0, 1};
    VkRect2D area{{0, 0}, {R.passWidth, R.passHeight}};
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    record::vkCmdSetViewport(cmd, 0, 1, &viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &area);
    record::vkCmdPushConstants(cmd, res.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof g, &g);
    record::vkCmdDraw(cmd, 3, 1, 0, 0);
    // the game's next draw must not trust its cached pipeline / dynamic state: start a fresh pass
    end_encoder();
}


// ---- cloud shadows (Graphics > Cloud shadows), as in metal_clouds.mm: after the draw that composites
// the sky buffer, the picture and its depth buffer are remembered; right before the picture is first
// read (the post effects) or a depth-only draw rewrites its depth, a full-screen pass reconstructs each
// pixel's world position from depth, follows the light up to the cloud layer and darkens by the cloud
// coverage there (the game's clouds anchored in the world), in two toon steps
namespace {
const char* kShadowFragment = R"glsl(#version 450
layout(push_constant) uniform G { vec4 view0, view1, view2, proj, sun, layer, look; } g;
layout(set=0, binding=0) uniform sampler2D depthTex;
layout(set=0, binding=1) uniform Clouds { vec4 c[100]; } cl;
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 color;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void main() {
    float d = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;
    if (d >= 0.99999) discard;
    if (int(g.look.z + 0.5) == 10) { color = vec4(1, 0, 1, 1); return; }  // debug: geometry in magenta
    if (int(g.look.z + 0.5) == 12) { color = vec4(clamp(cl.c[0].w, 0.0, 1.0), g.layer.w / 100.0, clamp(cl.c[0].z / 3000.0, 0.0, 1.0), 1); return; }
    float n = g.proj.z, f = g.proj.w;
    float zlin = n * f / (f - d * (f - n));  // GX2 depth is 0..1
    vec3 dc = vec3(ndc.x / g.proj.x, ndc.y / g.proj.y, -1.0);
    mat3 view = mat3(g.view0.xyz, g.view1.xyz, g.view2.xyz);
    vec3 p = transpose(view) * (dc * zlin);
    float wy = p.y + g.layer.z;
    vec3 L = g.sun.xyz;
    float t = (g.layer.x - wy) / max(L.y, 0.05);
    vec2 q = p.xz + L.xz * t;
    if (int(g.look.z + 0.5) == 13) { color = vec4(fract(q.x / 3000.0), fract(q.y / 3000.0), clamp(wy / 200.0, 0.0, 1.0), 1); return; }
    float cov = 0.0;
    for (int k = 0; k < int(g.layer.w); k++) {
        vec4 c = cl.c[k];
        vec2 dd = (q - c.xy) / c.z;
        float r2 = dot(dd, dd);
        if (r2 < 1.0) cov = max(cov, clamp(c.w * 1.3, 0.0, 1.0) * (1.0 - r2));
    }
    float nz = vnoise(q * 0.0011) * 0.6 + vnoise(q * 0.0034) * 0.4;
    cov = clamp((cov - 0.18 - (0.55 - nz) * 0.45) * 3.0, 0.0, 1.0);
    if (int(g.look.z + 0.5) == 4) { color = vec4(1.0, 1.0 - 0.8 * cov, 1.0 - 0.8 * cov, 1.0); return; }  // debug: coverage
    float sw = g.look.y;
    float shade = 0.45 * smoothstep(0.25 - sw, 0.25 + sw, cov) + 0.55 * smoothstep(0.6 - sw, 0.6 + sw, cov);
    float fade = 1.0 - smoothstep(12000.0, 22000.0, length(p.xz));
    float k = 1.0 - g.look.x * shade * fade * g.sun.w;
    if (k > 0.999) discard;
    color = vec4(k, k, k, 1.0);
}
)glsl";
struct ShadowGlobals { float view[12], proj[4], sun[4], layer[4], look[4]; };
struct ShadowResources {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::unordered_map<int, VkPipeline> pipelines;  // by colour format
} sres;
uint32_t g_pictureAddr = 0, g_pictureDepthAddr = 0, g_shadowSeq = 0;
Surface *g_picture = nullptr, *g_pictureDepth = nullptr;
uint32_t g_pictureSlice = 0;
uint64_t g_shadowFrame = ~0ull;

VkPipeline shadow_pipeline(VkFormat format) {
    if (sres.device && sres.device != R.device) sres = {};
    sres.device = R.device;
    if (!sres.set) {
        VkDescriptorSetLayoutBinding b[2]{{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                          {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 2;
        ci.pBindings = b;
        check_vk(vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &sres.set), "create cloud shadow descriptors");
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShadowGlobals)};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 1;
        li.pSetLayouts = &sres.set;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &push;
        check_vk(vkCreatePipelineLayout(R.device, &li, nullptr, &sres.layout), "create cloud shadow layout");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.minFilter = si.magFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check_vk(vkCreateSampler(R.device, &si, nullptr, &sres.sampler), "create cloud shadow sampler");
    }
    if (auto it = sres.pipelines.find(int(format)); it != sres.pipelines.end()) return it->second;
    VkShaderModule vs = sky_module(kSkyVertex, true), fs = sky_module(kShadowFragment, false);
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = i ? fs : vs;
        stages[i].pName = "main";
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;  // multiply: dst * k
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = 1;
    bs.pAttachments = &blend;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_targets(ci, rendering);
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &dss;
    ci.pColorBlendState = &bs;
    ci.pDynamicState = &ds;
    ci.layout = sres.layout;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe);
    vkDestroyShaderModule(R.device, vs, nullptr);
    vkDestroyShaderModule(R.device, fs, nullptr);
    if (r != VK_SUCCESS) pipe = VK_NULL_HANDLE;
    LOG("[shadows] Vulkan pipeline for format %d%s", int(format), pipe ? "" : " failed");
    sres.pipelines.emplace(int(format), pipe);
    return pipe;
}

VkImageView depth_view(Surface* s, uint32_t layer) {
    const uint32_t key = 0x9E000000u | layer;  // freed with the surface's other sampled views
    if (auto it = s->sampledViews.find(key); it != s->sampledViews.end()) return it->second;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = s->image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = s->fmt.pixel;
    info.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, layer, 1};
    VkImageView view = VK_NULL_HANDLE;
    check_vk(vkCreateImageView(R.device, &info, nullptr, &view), "create cloud shadow depth view");
    s->sampledViews.emplace(key, view);
    return view;
}

void shadows_now() {
    const uint32_t seq = g_shadowSeq;
    g_shadowSeq = 0;
    Surface* color = g_picture;
    Surface* depth = g_pictureDepth;
    // several surfaces can share an address: take the last pass's, where the later scene draws went
    if (R.passColors[0] && R.passColors[0]->addr == g_pictureAddr) color = R.passColors[0];
    if (R.passDepth && R.passDepth->addr == g_pictureDepthAddr) depth = R.passDepth;
    if (!color || !depth || !depth->fmt.depth) return;
    VkPipeline pipe = shadow_pipeline(color->fmt.pixel);
    if (!pipe) return;
    const cloud_proto::Snapshot& s = *cloud_proto::snapshot(seq);
    ShadowGlobals g{};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) g.view[c * 4 + r] = s.viewRot[r * 4 + c];
    const float aspect = float(color->extent.width) / float(color->extent.height);
    const float f = 1.0f / std::tan(s.fovy * 0.5f * 3.14159265f / 180.0f);
    g.proj[0] = f / aspect, g.proj[1] = f, g.proj[2] = s.viewNear, g.proj[3] = s.viewFar;
    cloud_proto::SkyFrame sf;
    cloud_proto::sky_frame(s, aspect, 0, sf);
    for (int i = 0; i < 4; i++) g.sun[i] = sf.sun[i];
    g.layer[0] = 6000.0f, g.layer[1] = 24000.0f, g.layer[2] = s.eye[1], g.layer[3] = float(s.nShadow);
    auto envf = [](const char* n, float d) { const char* e = getenv(n); return e && *e ? (float)atof(e) : d; };
    static const float dark = envf("WWHD_CLOUD_SHADOW_DARKNESS", 0.22f), debug = envf("WWHD_CLOUD_SHADOWS_DEBUG", 0.0f);
    g.look[0] = dark * (1.0f - 0.7f * sf.lit[3]) * std::clamp(1.0f - 0.6f * s.strength, 0.3f, 1.0f);
    g.look[1] = 0.04f, g.look[2] = debug;
    UploadSlice clouds = allocate_upload(sizeof(float) * 4 * 100, 256);
    std::memset(clouds.mapped, 0, sizeof(float) * 4 * 100);
    std::memcpy(clouds.mapped, s.shadow, sizeof(float) * 4 * size_t(std::max(0, s.nShadow)));
    end_encoder();
    transition_image(depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    transition_image(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = R.descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &sres.set;
    check_vk(vkAllocateDescriptorSets(R.device, &alloc, &set), "allocate cloud shadow descriptors");
    VkDescriptorImageInfo image{sres.sampler, depth_view(depth, R.passDepth == depth ? R.passDepthSlice : 0),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo buffer{clouds.buffer, clouds.offset, sizeof(float) * 4 * 100};
    VkWriteDescriptorSet w[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstSet = w[1].dstSet = set;
    w[0].dstBinding = 0, w[0].descriptorCount = 1, w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w[0].pImageInfo = &image;
    w[1].dstBinding = 1, w[1].descriptorCount = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, w[1].pBufferInfo = &buffer;
    vkUpdateDescriptorSets(R.device, 2, w, 0, nullptr);
    VkCommandBuffer cmd = command_buffer();
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = layer_view(color, g_pictureSlice);
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent = {color->extent.width, color->extent.height};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    begin_pass(cmd, ri, [&] {
        PassBegin pass;
        pass.formats.colorCount = 1;
        pass.formats.colors[0] = color->fmt.pixel;
        pass.layouts[0] = color->layout;
        return pass;
    });
    ++R.renderPassCount;
    VkViewport viewport{0, 0, float(color->extent.width), float(color->extent.height), 0, 1};
    VkRect2D area{{0, 0}, {color->extent.width, color->extent.height}};
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    record::vkCmdSetViewport(cmd, 0, 1, &viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &area);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sres.layout, 0, 1, &set, 0, nullptr);
    record::vkCmdPushConstants(cmd, sres.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof g, &g);
    record::vkCmdDraw(cmd, 3, 1, 0, 0);
    end_pass(cmd);
}
}  // namespace

// draw.cpp, once a draw's targets are known and before its textures are bound: notices the sky
// composite (it samples the sky buffer) and applies the pending cloud shadows before the picture is read
void clouds_before_draw(const uint32_t* r, const vk::Shader* ps, Surface* color0, uint32_t slice0, Surface* depth) {
    if (!render::cloud_shadows() || !ps || !ps->dec || g_skyFrame != R.frame) return;
    bool samplesSky = false, readsPicture = false;
    for (int u = 0; u < ps->dec->textureUnitListCount; u++) {
        const uint32_t unit = ps->dec->textureUnitList[u];
        const uint32_t addr = r[Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + unit * 7 + 2] << 8;
        if (addr && addr == g_skyAddr) samplesSky = true;
        if (g_shadowSeq && addr && addr == g_pictureAddr) readsPicture = true;
    }
    if (g_shadowSeq && !color0 && depth && depth->addr == g_pictureDepthAddr) readsPicture = true;
    static uint64_t composites = 0, applied = 0;
    if (readsPicture && g_shadowFrame == R.frame) shadows_now(), applied++;
    if (samplesSky) composites++;
    if (samplesSky && composites % 300 == 0) LOG("[shadows] Vulkan: composites %llu, applied %llu", (unsigned long long)composites, (unsigned long long)applied);
    if (samplesSky && color0 && depth && g_skySeq) {
        g_picture = color0, g_pictureAddr = color0->addr, g_pictureSlice = slice0;
        g_pictureDepth = depth, g_pictureDepthAddr = depth->addr;
        g_shadowSeq = g_skySeq, g_shadowFrame = R.frame;
        g_skySeq = 0;
    }
}

}  // namespace gfxvk
