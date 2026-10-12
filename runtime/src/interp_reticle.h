#pragma once
#include "interp_pacing.h"
#include <array>
#include <cstdint>

namespace interp::reticle {
using Point = std::array<float, 3>;

// Screen endpoints belong to logic steps, not draw passes. Hold projections
// may use the preceding render pass's camera and must not replace the endpoint.
struct History {
    Point previous{}, current{};
    uint64_t step = ~uint64_t(0);
    uint32_t source = 0;
    bool valid = false;

    Point sample(Point exact, uint64_t now, uint32_t identity, float t) {
        if (now != step || identity != source) {
            valid = source == identity && step != ~uint64_t(0) && step + 1 == now;
            previous = current;
            step = now;
            source = identity;
            current = exact;
        }
        if (!valid || t >= 1.0f) return current;
        Point result;
        for (int i = 0; i < 3; ++i)
            result[i] = pacing::lerp_f(previous[i], current[i], t);
        return result;
    }
};
} // namespace interp::reticle
