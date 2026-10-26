// SPDX-License-Identifier: MPL-2.0
// Identify special rendering passes by their program, wherever the game loads it.
#pragma once
#include "area_sample.h"

namespace gfx {
// PointLight: the passes that light the half-size point-light buffer (PS 44390C00, 44461F00); the buffer and
// the depth copy they read get as many pixels as the screen with Full-size torchlight (gfx/area_sample.h)
enum class ProgramKind { Other, DepthDownsample, OcclusionPixel, OcclusionVertex, SunShadowMask, PointLight };
inline ProgramKind program_kind(const void* bytes, uint32_t size, bool vertex) {
    if (!bytes) return ProgramKind::Other;
    if (vertex && size == 384 && area_sample::program_hash(bytes, size) == 0xd279c909832a6f45ull)
        return ProgramKind::OcclusionVertex;
    if (!vertex && size == 416 && area_sample::program_hash(bytes, size) == 0xf49059587d7d9669ull)
        return ProgramKind::DepthDownsample;
    if (!vertex && size == 1584 && area_sample::program_hash(bytes, size) == 0xc2cae30e906b6255ull)
        return ProgramKind::OcclusionPixel;
    if (!vertex && size == 2112 && area_sample::program_hash(bytes, size) == 0x62ae9ab4cb625546ull)
        return ProgramKind::SunShadowMask;
    if (!vertex && size == 560 && area_sample::program_hash(bytes, size) == 0xf79ca4602184e6ebull)
        return ProgramKind::PointLight;
    if (!vertex && size == 688 && area_sample::program_hash(bytes, size) == 0x4a76609b1006452aull)
        return ProgramKind::PointLight;
    return ProgramKind::Other;
}
} // namespace gfx
