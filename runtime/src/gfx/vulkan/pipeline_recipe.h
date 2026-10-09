#pragma once
#include "pipeline_state.h"
#include <array>
#include <memory>

namespace gfxvk {
// Device/driver/features and schema compatibility are checked before a recipe
// reaches Vulkan. Neither Vulkan handles nor host pointers are serialized.
using PipelineRecipeFingerprint = std::array<uint8_t, 48>;
struct PipelineRecipe {
  std::vector<uint8_t> key;
  std::array<std::vector<uint32_t>, 2> spirv;
  std::array<std::vector<VkDescriptorSetLayoutBinding>, 2> descriptors;
  std::unique_ptr<GraphicsPipelineState> state;
  bool dynamicUniforms = false;
};
std::vector<uint8_t> encode_pipeline_recipe(PipelineRecipe& recipe);
std::shared_ptr<PipelineRecipe> decode_pipeline_recipe(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> encode_pipeline_recipes(const PipelineRecipeFingerprint& fingerprint,
    const std::vector<std::shared_ptr<PipelineRecipe>>& recipes);
std::vector<std::shared_ptr<PipelineRecipe>> decode_pipeline_recipes(
    const PipelineRecipeFingerprint& fingerprint, const std::vector<uint8_t>& bytes);
}
