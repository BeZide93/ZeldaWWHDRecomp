// SPDX-License-Identifier: MPL-2.0
// The event list a mod demo plays through: one event with one PACKAGE staff whose PLAY cut names the
// demo's STB file (dEvDtStaff_c::specialProcPackage loads it from the demo archive, starts it with
// dDemo_manager_c::create and, when the data runs out, removes the demo and ends the cut itself).
// Same pool layout as the game's event_list.dat (event 0xB0, staff/cut 0x50, property 0x40 bytes).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace demo_event_image {
struct Origin { float x = 0, y = 0, z = 0, angle_y = 0; };
struct Spec {
    std::string event = "MOD_DEMO";     // event name (<32)
    std::string file = "moddemo.stb";   // STB name inside the (virtual) demo archive (<32)
    std::optional<Origin> origin;       // OffsetPos / OffsetAngY: tracks relative to this point
};
constexpr uint32_t kEventStateOffset = 0x40 + 0xA4;  // the event's run state word inside the image
bool make(const Spec& spec, std::vector<uint8_t>& out, std::string& why);
// Reads a made image back; accepts only what make() writes.
bool validate(const std::vector<uint8_t>& image, Spec& out, std::string& why);
}  // namespace demo_event_image
