#include "gfx/target_size.h"
#include <cassert>

int main() {
    using gfx::TargetSize;
    gfx::SizeClaim target;
    target.request(TargetSize::Internal, 5);
    assert(target.size == TargetSize::Internal);  // an ordinary target
    target.request(TargetSize::Console, 100);
    assert(target.size == TargetSize::Console);
    target.request(TargetSize::Internal, 100);  // another pass into the claimed target, same frame
    assert(target.size == TargetSize::Console);
    target.request(TargetSize::Internal, 100 + gfx::SizeClaim::kIdleFrames);
    assert(target.size == TargetSize::Console);
    target.request(TargetSize::Console, 105);  // each claim renews it
    target.request(TargetSize::Internal, 105 + gfx::SizeClaim::kIdleFrames);
    assert(target.size == TargetSize::Console);
    target.request(TargetSize::Internal, 106 + gfx::SizeClaim::kIdleFrames);  // left by its claimer
    assert(target.size == TargetSize::Internal);  // another target at the address
    assert(target.size == TargetSize::Internal);
    target.request(TargetSize::Console, 121);  // claimed again
    assert(target.size == TargetSize::Console && target.frame == 121);
}
