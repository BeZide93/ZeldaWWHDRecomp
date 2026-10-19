// SPDX-License-Identifier: MPL-2.0
// A mod scene's "demo" step: keyframe tracks for the camera, game actors (by stage name, Link
// included), models from the player's game archives, sounds, particles and messages, compiled into
// a JStudio STB (demo_stb.h) that the game's own demo system plays. The manifest is parsed and
// checked at load; the STB is built when the step starts, once the game's resource ids are known.
//
// {"type":"demo","frames":240,"origin":"link"|{"x","y","z"},"angle":0,
//  "camera":{"eye":TRACK,"target":TRACK,"fovy":TRACK,"roll":TRACK},
//  "actors":[{"id":"bird","actor":"Dk"|"Link"|"model":{"archive":"Kamome","file":"ka.bdl"},
//             "position":TRACK,"rotation":TRACK,"scale":TRACK,
//             "clips":[{"frame":0,"archive":"Dk","file":"fly1.bck","speed":1}],
//             "modes":[{"frame":0,"mode":2,"data":"hex"}],          (Link: demo modes)
//             "data":[{"frame":0,"id":1,"bytes":"hex"}]}],           (raw actor data paragraphs)
//  "sounds":[{"frame":30,"id":22820,"actor":"bird"|"position":{..},"volume":1,"pan":0.5}],
//  "particles":[{"frame":50,"id":33059,"actor":"bird","joint":"j_dk_ashi_r3"|"position":{..}}],
//  "messages":[{"frame":60,"text":"..."}]}
// TRACK = {"curve":"linear"|"smooth"|"bspline"|"step","keys":[{"frame":0,"value":{x,y,z} or number}]}
// Frames are the game's 30 Hz demo frames.
#pragma once
#include "mod_json.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace demo_scene {
inline constexpr uint32_t kMaxFrames = 9000, kMaxActors = 24, kMaxKeys = 64, kMaxEvents = 64, kMaxStbBytes = 0xC000;
struct Vec { float x = 0, y = 0, z = 0; bool operator==(const Vec&) const = default; };
enum class Curve { Linear, Smooth, BSpline, Step };
struct Key { uint32_t frame = 0; Vec value; bool operator==(const Key&) const = default; };
struct Track { Curve curve = Curve::Linear; std::vector<Key> keys; bool operator==(const Track&) const = default; };  // scalars use value.x
struct Resource { std::string archive, file; bool operator==(const Resource&) const = default; };
struct Clip { uint32_t frame = 0; Resource resource; float speed = 1; bool operator==(const Clip&) const = default; };
struct Mode { uint32_t frame = 0, mode = 0; std::vector<uint8_t> data; bool operator==(const Mode&) const = default; };
struct Data { uint32_t frame = 0, id = 0; std::vector<uint8_t> bytes; bool operator==(const Data&) const = default; };
struct Actor {
    enum class Kind { Game, Link, Model } kind = Kind::Game;
    std::string id, actor;  // demo-local id; the game's stage actor name (Game, Link)
    Resource model;         // Model
    Track position, rotation, scale;
    std::vector<Clip> clips;
    std::vector<Mode> modes;
    std::vector<Data> data;
    bool operator==(const Actor&) const = default;
};
struct Sound { uint32_t frame = 0, id = 0; std::string actor; std::optional<Vec> position; float volume = 1, pan = 0.5f; bool operator==(const Sound&) const = default; };
struct Particle { uint32_t frame = 0, id = 0; std::string actor, joint; std::optional<Vec> position; bool operator==(const Particle&) const = default; };
struct Message { uint32_t frame = 0; std::string text; bool operator==(const Message&) const = default; };
struct Demo {
    uint32_t frames = 0;
    bool origin_link = false;
    std::optional<Vec> origin;
    float angle = 0;
    Track eye, target, fovy, roll;
    std::vector<Actor> actors;
    std::vector<Sound> sounds;
    std::vector<Particle> particles;
    std::vector<Message> messages;
    bool operator==(const Demo&) const = default;
};
inline bool parse(const mods::json::Value& step, Demo& out, std::string& why);  // demo_scene_parse.h

// The game's id of a file in one of its archives (the archive's resource id table), or -1.
using Resolver = std::function<int32_t(const Resource&)>;
struct Compiled {
    std::vector<uint8_t> stb;
    std::vector<std::pair<Resource, uint32_t>> virtual_ids;  // virtual id n (0x10000|n in the STB) -> archive file and its id
    std::vector<std::string> participants;                   // stage actor names that must exist when the demo starts
    std::vector<Message> messages;                           // in frame order; one suspend point each
};
// message_number: the game message the demo's JMSG track shows (the port arms its text before each
// message point); the demo waits one frame later until the player closes it (the game's own unsuspend).
bool compile(const Demo& demo, const Resolver& resolve, Compiled& out, std::string& why, uint32_t message_number = 0x2CF3);
}  // namespace demo_scene
#include "demo_scene_parse.h"
