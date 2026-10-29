// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include "../runtime.h"
namespace enhanced {
float camera_distance();
void set_camera_distance(float value);
bool distance_gesture(int phase, float pixels);
// Applies only during drawing, after upstream interpolation chose the eye. Returns the drawn
// eye for upstream's sky/audio history; restores guest eye and CPU even when a guest call throws.
std::array<float, 3> draw_camera_distance(Cpu* cpu, void (*draw)(Cpu*));
}
