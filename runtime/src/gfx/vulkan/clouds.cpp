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
void sky_clouds_begin(const uint32_t* regs, uint32_t seq) {
    (void)regs;
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

}  // namespace gfxvk
