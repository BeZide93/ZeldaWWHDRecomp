#include "gfx/vulkan/pipeline_recipe.h"
#include "pipeline_state_fixture.h"
#include <cassert>
#include <thread>

using gfxvk::GraphicsPipelineState;
using pipeline_test::state;

using namespace gfxvk;
template<class F> static void rejects(F operation) {
  bool rejected = false;
  try { operation(); } catch (const std::runtime_error&) { rejected = true; }
  assert(rejected);
}
int main() {
  auto recipe = std::make_shared<PipelineRecipe>();
  recipe->state = std::make_unique<GraphicsPipelineState>(state());
  recipe->key.resize(208);
  for (size_t i = 0; i < recipe->key.size(); ++i) recipe->key[i] = uint8_t(i);
  // Structurally valid minimal SPIR-V is enough for serialization tests;
  // driver validation is tested separately with actual translated shaders.
  for (auto& code : recipe->spirv) code = {0x07230203, 0x00010300, 0, 1, 0, 0x00010000};
  recipe->dynamicUniforms = true;
  for (int stage = 0; stage < 2; ++stage)
    recipe->descriptors[stage].push_back({7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
      VkShaderStageFlags(stage ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT), nullptr});
  auto encoded = encode_pipeline_recipe(*recipe);
  // Portable state has the exact fixed-state wire fields, with no shader words.
  auto portable = encoded;
  size_t offset = 4 + 208 * 4 + 4;
  for (int stage = 0; stage < 2; ++stage) {
    portable.erase(portable.begin() + offset + 4, portable.begin() + offset + 4 + 6 * 4);
    for (int i = 0; i < 4; ++i) portable[offset+i] = 0;
    offset += 4 + 4 + 4 * 4; // empty code, one descriptor
  }
  portable.insert(portable.begin(), {'W','W','V','K','S','T','0','1'});
  auto codeFree = decode_code_free_pipeline_recipe(portable);
  assert(codeFree->spirv[0].empty() && codeFree->spirv[1].empty());
  assert(codeFree->key == recipe->key);
  assert(codeFree->descriptors[1][0].binding == 7);
  assert(codeFree->state->get()->pVertexInputState->pVertexBindingDescriptions[0].stride == 24);
  auto codeCarrying = encoded;
  codeCarrying.insert(codeCarrying.begin(), {'W','W','V','K','S','T','0','1'});
  rejects([&] { decode_code_free_pipeline_recipe(codeCarrying); });
  auto portableTrailing = portable; portableTrailing.push_back(0);
  rejects([&] { decode_code_free_pipeline_recipe(portableTrailing); });
  auto portableTruncated = portable; portableTruncated.pop_back();
  rejects([&] { decode_code_free_pipeline_recipe(portableTruncated); });
  rejects([&] { decode_pipeline_recipe(portable); });
  codeFree->spirv = recipe->spirv; // Only locally resolved words make a native recipe.
  assert(encode_pipeline_recipe(*codeFree) == encoded);
  auto restored = decode_pipeline_recipe(encoded);
  recipe.reset(); // No pointer in the decoded state may borrow from the original.
  assert(restored->key[207] == 207);
  assert(restored->dynamicUniforms);
  assert(restored->state->get()->pRasterizationState->depthBiasConstantFactor == 2.5f);
  assert(restored->state->get()->pVertexInputState->pVertexBindingDescriptions[0].stride == 24);
  assert(encode_pipeline_recipe(*restored) == encoded);
  PipelineRecipeFingerprint fingerprint{};
  auto database = encode_pipeline_recipes(fingerprint, {restored});
  assert(decode_pipeline_recipes(fingerprint, database).size() == 1);
  // Full-game populations exceed the old 512-route limit, but remain bounded.
  std::vector<std::shared_ptr<PipelineRecipe>> tooMany(16385, restored);
  rejects([&] { encode_pipeline_recipes(fingerprint, tooMany); });
  std::vector<std::shared_ptr<PipelineRecipe>> fullGame;
  for (size_t i = 0; i < 513; ++i) {
    auto entry = decode_pipeline_recipe(encoded);
    entry->key[0] = uint8_t(i); entry->key[1] = uint8_t(i >> 8);
    fullGame.push_back(std::move(entry));
  }
  auto fullGameDatabase = encode_pipeline_recipes(fingerprint, fullGame);
  assert(decode_pipeline_recipes(fingerprint, fullGameDatabase).size() == 513);
  auto duplicate = encode_pipeline_recipes(fingerprint, {restored, restored});
  rejects([&] { decode_pipeline_recipes(fingerprint, duplicate); });
  fingerprint[0] = 1;
  rejects([&] { decode_pipeline_recipes(fingerprint, database); });
  fingerprint[0] = 0;
  for (size_t size : {size_t(0), size_t(63), database.size() - 1}) {
    auto truncated = database; truncated.resize(size);
    rejects([&] { decode_pipeline_recipes(fingerprint, truncated); });
  }
  auto corrupt = database; corrupt.back() ^= 1;
  rejects([&] { decode_pipeline_recipes(fingerprint, corrupt); });
  auto trailing = database; trailing.push_back(0);
  rejects([&] { decode_pipeline_recipes(fingerprint, trailing); });
  auto shortened = encoded; shortened.pop_back();
  rejects([&] { decode_pipeline_recipe(shortened); });
  restored->descriptors[0].push_back(restored->descriptors[0].front());
  rejects([&] { encode_pipeline_recipe(*restored); });
  restored->descriptors[0].pop_back();
  restored->spirv[0].back() = 0;
  rejects([&] { encode_pipeline_recipe(*restored); });
  // Oversized byte scalars must not truncate to a valid key byte.
  auto noncanonical = encoded; noncanonical[5] = 1;
  rejects([&] { decode_pipeline_recipe(noncanonical); });
}
