#pragma once
#include <vulkan/vulkan.h>
#include <cstring>

namespace gfxvk {
// Qualcomm's proprietary version uses Vulkan's major/minor/patch packing;
// its driver release (e.g. 502) is the minor field. Mesa Turnip is excluded.
inline bool serial_pipelines_wanted(VkDriverId driver, uint32_t version, const char* overrideValue) {
  if (overrideValue && !std::strcmp(overrideValue, "0")) return false;
  if (overrideValue && !std::strcmp(overrideValue, "1")) return true;
  return driver == VK_DRIVER_ID_QUALCOMM_PROPRIETARY && VK_VERSION_MINOR(version) < 700;
}
} // namespace gfxvk
