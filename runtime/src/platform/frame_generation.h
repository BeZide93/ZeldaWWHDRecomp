// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
namespace enhanced::frame_generation {
// Set only after the renderer successfully loaded LSFG; ordinary builds/modes stay at one.
inline std::atomic<int> multiplier{1};
inline std::atomic<double> output_fps{0};
}
