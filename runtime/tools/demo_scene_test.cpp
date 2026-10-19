// SPDX-License-Identifier: MPL-2.0
// Mod demo step: manifest parsing, checks and the compiled STB (read back with demo_stb).
#include "mods/demo_event_image.h"
#include "mods/demo_scene.h"
#include "mods/demo_stb.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace D = demo_scene;
static bool has(const std::string& text, const std::string& line) { return text.find(line) != std::string::npos; }
static D::Demo parsed(const std::string& json) {
    D::Demo d;
    std::string why;
    if (!D::parse(mods::json::parse(json), d, why)) { std::fprintf(stderr, "parse: %s\n", why.c_str()); assert(false); }
    return d;
}
static std::string refused(const std::string& json) {
    D::Demo d;
    std::string why;
    assert(!D::parse(mods::json::parse(json), d, why));
    return why;
}

static const char* kBird = R"({"type":"demo","frames":240,"origin":"link",
 "camera":{"eye":{"keys":[{"frame":0,"value":{"x":0,"y":700,"z":-1200}}]},
           "target":{"curve":"linear","keys":[{"frame":0,"value":{"x":-2500,"y":1800,"z":3000}},{"frame":240,"value":{"x":2500,"y":900,"z":3000}}]},
           "fovy":{"keys":[{"frame":0,"value":55}]}},
 "actors":[{"id":"bird","actor":"Dk",
            "position":{"curve":"linear","keys":[{"frame":0,"value":{"x":-2500,"y":1800,"z":3000}},{"frame":240,"value":{"x":2500,"y":900,"z":3000}}]},
            "rotation":{"keys":[{"frame":0,"value":{"x":0,"y":90,"z":0}}]},
            "clips":[{"frame":0,"archive":"Dk","file":"fly1.bck"}]},
           {"id":"gull","model":{"archive":"Kamome","file":"ka.bdl"},
            "position":{"curve":"smooth","keys":[{"frame":0,"value":{"x":1500,"y":600,"z":1500}},{"frame":120,"value":{"x":0,"y":650,"z":1500}},{"frame":240,"value":{"x":-1500,"y":700,"z":1500}}]},
            "clips":[{"frame":0,"archive":"Kamome","file":"ka_fly1.bck","speed":1.5}]},
           {"id":"hero","actor":"Link","modes":[{"frame":0,"mode":2},{"frame":100,"mode":512,"data":"3a03008f002a003439030000"}]}],
 "sounds":[{"frame":30,"id":22820,"actor":"bird","volume":0.8}],
 "particles":[{"frame":50,"id":33059,"actor":"bird","joint":"j_dk_ashi_r3"}],
 "messages":[{"frame":60,"text":"Help!"},{"frame":150,"text":"Big brother!"}]})";

int main() {
    const D::Demo d = parsed(kBird);
    assert(d.frames == 240 && d.origin_link && d.actors.size() == 3 && d.messages.size() == 2);
    // the game's ids of the referenced files (from its resource id tables)
    const D::Resolver ids = [](const D::Resource& r) -> int32_t {
        if (r.archive == "Dk" && r.file == "fly1.bck") return 0x11;
        if (r.archive == "Kamome" && r.file == "ka.bdl") return 0x17;
        if (r.archive == "Kamome" && r.file == "ka_fly1.bck") return 0xD;
        return -1;
    };
    D::Compiled c;
    std::string why;
    assert(D::compile(d, ids, c, why));
    assert(c.participants == (std::vector<std::string>{"Dk", "Link"}));
    assert(c.virtual_ids.size() == 3 && c.virtual_ids[0].second == 0x11 && c.virtual_ids[1].second == 0x17 && c.virtual_ids[2].second == 0xD);
    demo_stb::File f;
    assert(demo_stb::parse(c.stb, f, why));
    const std::string t = demo_stb::dump(f);
    std::fputs(t.c_str(), stdout);
    assert(has(t, "== JCMR \"camera\"\n  f0 EYE_XYZ = 0,700,-1200\n  f0 TGT_XYZ = curves 0,1,2\n  f0 FOVY = 55\n  f240 TGT_XYZ = 2500,900,3000\n  f240 end\n"));
    assert(has(t, "fn #0 type 5 p18:0,8 p22:00000001 p1:n=2/2[0,-2500,8,2500]"));  // 240 frames = 8 s
    assert(has(t, "== JACT \"Dk\"\n  f0 TXYZ = curves 3,4,5\n  f0 RXYZ = 0,90,0\n  f0 ANIMATION = u32 65536\n  f0 ANIM_MODE = int 1\n  f0 ANIM_FRAME = rate 30\n  f240 TXYZ = 2500,900,3000\n  f240 end\n"));
    assert(has(t, "== JACT \"d_act0\"\n  f0 SHAPE = u32 65537\n  f0 TXYZ = curves 6,7,8\n  f0 ANIMATION = u32 65538\n  f0 ANIM_MODE = int 1\n  f0 ANIM_FRAME = rate 45\n"));
    assert(has(t, "fn #6 type 6 p18:0,8 p1:n=3/4[0,1500,-375,-375,4,0,-375,-375,8,-1500,-375,-375]"));  // smooth: Catmull-Rom slopes per second
    assert(has(t, "== JACT \"Link\"\n  f0 ANIMATION = u32 2\n  f100 ANIMATION = u32 512\n  f100 ANIM_MODE = int 1\n  f100 ANIM_FRAME = rate 30\n  f100 reserved 129 00000004000000013a03008f002a003439030000\n  f240 end\n"));
    assert(has(t, "== JSND \"se0\"\n  f0 XYZ = curves 9,10,11\n  f30 SOUND = u32 22820\n  f30 VOLUME = 0.8\n  f30 PAN = 0.5\n  f240 XYZ = 2500,900,3000\n"));
    assert(has(t, "== JPTC \"efx0\"\n  f50 PARENT = name Dk\n  f50 PARENT_NODE = name j_dk_ashi_r3\n  f50 PARENT_ENABLE = int 1\n  f50 PARTICLE = u32 33059\n"));
    assert(has(t, "== JMSG \"message\"\n  f60 MESSAGE = u32 11507\n  f150 MESSAGE = u32 11507\n  f240 end\n"));
    assert(has(t, "== CTRL \"control\"\n  f61 seq 4 1\n  f151 seq 4 1\n  f151 end\n"));  // waits one frame after each message

    // a file the game doesn't have stops the build with a reason
    D::Compiled missing;
    assert(!D::compile(d, [](const D::Resource&) { return int32_t(-1); }, missing, why) && has(why, "the game has no fly1.bck in archive Dk"));

    // refusals with reasons
    assert(has(refused(R"({"type":"demo","frames":0})"), "frames"));
    assert(has(refused(R"({"type":"demo","frames":30,"bogus":1})"), "unknown demo field"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","model":{"archive":"Demo04","file":"ka.bdl"}}]})"), "invalid archive name"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"d_act1"}]})"), "invalid stage actor name"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk","position":{"keys":[{"frame":20,"value":{"x":0,"y":0,"z":0}},{"frame":10,"value":{"x":0,"y":0,"z":0}}]}}]})"), "key frames must increase"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Link","clips":[{"frame":0,"archive":"Link","file":"a.bck"}]}]})"), "Link plays demo modes"));
    assert(has(refused(R"({"type":"demo","frames":30,"camera":{"eye":{"keys":[{"frame":0,"value":{"x":0,"y":0,"z":0}}]}}})"), "eye and target together"));
    assert(has(refused(R"({"type":"demo","frames":30,"messages":[{"frame":30,"text":"late"}]})"), "frame before the end"));
    assert(has(refused(R"({"type":"demo","frames":30,"sounds":[{"frame":0,"id":5,"actor":"nobody"}],"actors":[{"id":"a","actor":"Dk"}]})"), "unknown actor nobody"));

    // the event image that plays it: one PACKAGE staff, PLAY cut naming the STB file
    demo_event_image::Spec spec;
    spec.origin = demo_event_image::Origin{668, 95, -197304, 0};
    std::vector<uint8_t> image;
    assert(demo_event_image::make(spec, image, why));
    demo_event_image::Spec back;
    assert(demo_event_image::validate(image, back, why) && back.file == "moddemo.stb" && back.origin && back.origin->z == -197304.f);
    assert(std::string(reinterpret_cast<const char*>(image.data() + 0xF0)) == "PACKAGE" && std::string(reinterpret_cast<const char*>(image.data() + 0x140)) == "PLAY");
    assert(image[0xF0 + 0x2C + 3] == 11);  // staff type PACKAGE
    image[0x140 + 0x3C] = 0;              // any other byte is refused
    assert(!demo_event_image::validate(image, back, why));
    spec.file = "../x";
    assert(!demo_event_image::make(spec, image, why));
    spec = {};
    assert(demo_event_image::make(spec, image, why) && demo_event_image::validate(image, back, why) && !back.origin);
    std::puts("demo_scene: ok");
    return 0;
}
