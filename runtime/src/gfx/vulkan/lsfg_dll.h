// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace gfxvk::fg::dll {
constexpr uint32_t kShaders = 49;
struct Shader { std::vector<uint32_t> spirv, bindings; };
// Bounded PE/SPIR-V parser. No execution or DLL loading, no bundled shader binaries.
bool read(const std::string& path, Shader* out, std::string& error);
}
