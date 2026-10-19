// SPDX-License-Identifier: MPL-2.0
#include "demo_scene.h"

#include "demo_stb.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace demo_scene {
namespace {
using mods::json::Value;
bool fail(std::string& why, const std::string& text) { why = text; return false; }
bool keys_only(const Value& v, std::initializer_list<const char*> allowed) {
    if (v.type != Value::Object) return false;
    for (const auto& [key, value] : v.object) {
        bool found = false;
        for (const char* a : allowed) found |= key == a;
        if (!found) return false;
    }
    return true;
}
bool integer(const Value& v, double lo, double hi, uint32_t& out) {
    if (v.type != Value::Number || !std::isfinite(v.number) || v.number < lo || v.number > hi || std::floor(v.number) != v.number) return false;
    out = uint32_t(v.number);
    return true;
}
bool real(const Value& v, double lo, double hi, float& out) {
    if (v.type != Value::Number || !std::isfinite(v.number) || v.number < lo || v.number > hi) return false;
    out = float(v.number);
    return true;
}
bool vec(const Value& v, Vec& out) {
    if (!keys_only(v, {"x", "y", "z"})) return false;
    return real(v.get("x"), -1e6, 1e6, out.x) && real(v.get("y"), -1e6, 1e6, out.y) && real(v.get("z"), -1e6, 1e6, out.z);
}
bool plain(const std::string& s, size_t max, bool dots = false) {
    if (s.empty() || s.size() > max) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || (dots && c == '.'))) return false;
    return true;
}
bool hexbytes(const Value& v, std::vector<uint8_t>& out, size_t max) {
    if (v.type != Value::String || v.text.size() % 2 || v.text.size() / 2 > max) return false;
    out.clear();
    for (size_t i = 0; i < v.text.size(); i += 2) {
        auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        const int a = nibble(v.text[i]), b = nibble(v.text[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back(uint8_t(a << 4 | b));
    }
    return true;
}
// Demo archive names the HD game special-cases by name (manager create/update): never reused.
bool reserved_archive(const std::string& a) { return a == "Demo04" || a == "Demo08" || a == "Demo29" || a == "Demo37" || a == "Demo45" || a == "Demo46"; }
bool resource(const Value& v, const char* ext, Resource& out, std::string& why) {
    if (!keys_only(v, {"archive", "file"}) && !keys_only(v, {"frame", "archive", "file", "speed"})) return fail(why, "resource needs archive and file");
    out.archive = v.get("archive").string();
    out.file = v.get("file").string();
    if (!plain(out.archive, 11) || reserved_archive(out.archive)) return fail(why, "invalid archive name \"" + out.archive + "\"");
    if (!plain(out.file, 31, true) || out.file.size() < 5 || out.file.compare(out.file.size() - 4, 4, ext) != 0) return fail(why, std::string("resource file must be a ") + ext + " name");
    return true;
}
bool track(const Value& v, bool scalar, uint32_t frames, Track& out, std::string& why, const char* what) {
    out = {};
    if (v.type == Value::Null) return true;
    if (!keys_only(v, {"curve", "keys"})) return fail(why, std::string(what) + ": track needs curve and keys");
    const std::string curve = v.get("curve").string("linear");
    if (curve == "linear") out.curve = Curve::Linear;
    else if (curve == "smooth") out.curve = Curve::Smooth;
    else if (curve == "bspline") out.curve = Curve::BSpline;
    else if (curve == "step") out.curve = Curve::Step;
    else return fail(why, std::string(what) + ": curve must be linear, smooth, bspline or step");
    const auto& keys = v.get("keys");
    if (keys.type != Value::Array || keys.array.empty() || keys.array.size() > kMaxKeys) return fail(why, std::string(what) + ": 1..64 keys");
    for (const auto& k : keys.array) {
        Key key;
        if (!keys_only(k, {"frame", "value"}) || !integer(k.get("frame"), 0, frames, key.frame)) return fail(why, std::string(what) + ": key needs a frame inside the demo");
        if (scalar ? !real(k.get("value"), -1e6, 1e6, key.value.x) : !vec(k.get("value"), key.value)) return fail(why, std::string(what) + ": key needs a value");
        if (!out.keys.empty() && key.frame <= out.keys.back().frame) return fail(why, std::string(what) + ": key frames must increase");
        out.keys.push_back(key);
    }
    return true;
}
template <class T> bool frame_order(const std::vector<T>& v) { return std::is_sorted(v.begin(), v.end(), [](const T& a, const T& b) { return a.frame < b.frame; }); }
}  // namespace

bool parse(const Value& st, Demo& d, std::string& why) {
    d = {};
    if (!keys_only(st, {"type", "frames", "origin", "angle", "camera", "actors", "sounds", "particles", "messages"}) || st.get("type").string() != "demo")
        return fail(why, "unknown demo field");
    if (!integer(st.get("frames"), 1, kMaxFrames, d.frames)) return fail(why, "demo needs frames 1..9000");
    const auto& origin = st.get("origin");
    if (origin.type == Value::String) {
        if (origin.text != "link") return fail(why, "demo origin must be \"link\" or a position");
        d.origin_link = true;
    } else if (origin.type != Value::Null) {
        Vec o;
        if (!vec(origin, o)) return fail(why, "invalid demo origin");
        d.origin = o;
    }
    if (st.object.contains("angle") && !real(st.get("angle"), -360, 360, d.angle)) return fail(why, "demo angle must be -360..360");
    const auto& cam = st.get("camera");
    if (cam.type != Value::Null) {
        if (!keys_only(cam, {"eye", "target", "fovy", "roll"})) return fail(why, "unknown demo camera field");
        if (!track(cam.get("eye"), false, d.frames, d.eye, why, "camera eye") || !track(cam.get("target"), false, d.frames, d.target, why, "camera target") ||
            !track(cam.get("fovy"), true, d.frames, d.fovy, why, "camera fovy") || !track(cam.get("roll"), true, d.frames, d.roll, why, "camera roll"))
            return false;
        if (d.eye.keys.empty() != d.target.keys.empty()) return fail(why, "demo camera needs eye and target together");
        for (const auto& k : d.fovy.keys) if (k.value.x < 5 || k.value.x > 120) return fail(why, "camera fovy must be 5..120");
    }
    const auto& actors = st.get("actors");
    if (actors.type != Value::Null) {
        if (actors.type != Value::Array || actors.array.size() > kMaxActors) return fail(why, "demo actors must be a list of up to 24");
        std::set<std::string> ids, names;
        for (const auto& a : actors.array) {
            Actor x;
            if (!keys_only(a, {"id", "actor", "model", "position", "rotation", "scale", "clips", "modes", "data"})) return fail(why, "unknown demo actor field");
            x.id = a.get("id").string();
            if (!plain(x.id, 31) || !ids.insert(x.id).second) return fail(why, "demo actor needs a unique id");
            if (a.object.contains("actor") == a.object.contains("model")) return fail(why, "demo actor " + x.id + ": actor or model, exclusively");
            if (a.object.contains("model")) {
                x.kind = Actor::Kind::Model;
                if (!resource(a.get("model"), ".bdl", x.model, why)) return fail(why, "demo actor " + x.id + ": " + why);
            } else {
                x.actor = a.get("actor").string();
                if (!plain(x.actor, 7) || x.actor.rfind("d_act", 0) == 0) return fail(why, "demo actor " + x.id + ": invalid stage actor name");
                if (!names.insert(x.actor).second) return fail(why, "demo actor " + x.actor + " listed twice");
                x.kind = x.actor == "Link" ? Actor::Kind::Link : Actor::Kind::Game;
            }
            if (!track(a.get("position"), false, d.frames, x.position, why, "actor position") || !track(a.get("rotation"), false, d.frames, x.rotation, why, "actor rotation") ||
                !track(a.get("scale"), false, d.frames, x.scale, why, "actor scale"))
                return fail(why, "demo actor " + x.id + ": " + why);
            const auto& clips = a.get("clips");
            if (clips.type != Value::Null) {
                if (x.kind == Actor::Kind::Link) return fail(why, "Link plays demo modes, not clips");
                if (clips.type != Value::Array || clips.array.size() > kMaxEvents) return fail(why, "demo actor " + x.id + ": clips must be a list");
                for (const auto& c : clips.array) {
                    Clip clip;
                    if (!keys_only(c, {"frame", "archive", "file", "speed"}) || !integer(c.get("frame"), 0, d.frames, clip.frame)) return fail(why, "demo actor " + x.id + ": clip needs a frame");
                    if (!resource(c, ".bck", clip.resource, why)) return fail(why, "demo actor " + x.id + ": " + why);
                    if (c.object.contains("speed") && !real(c.get("speed"), 0, 8, clip.speed)) return fail(why, "clip speed must be 0..8");
                    x.clips.push_back(clip);
                }
                if (!frame_order(x.clips)) return fail(why, "demo clips must be in frame order");
            }
            const auto& modes = a.get("modes");
            if (modes.type != Value::Null) {
                if (x.kind != Actor::Kind::Link) return fail(why, "only Link has demo modes");
                if (modes.type != Value::Array || modes.array.size() > kMaxEvents) return fail(why, "Link modes must be a list");
                for (const auto& m : modes.array) {
                    Mode mode;
                    if (!keys_only(m, {"frame", "mode", "data"}) || !integer(m.get("frame"), 0, d.frames, mode.frame) || !integer(m.get("mode"), 1, 0xFFFF, mode.mode))
                        return fail(why, "Link mode needs frame and mode");
                    if (m.object.contains("data") && !hexbytes(m.get("data"), mode.data, 64)) return fail(why, "Link mode data must be hex bytes");
                    x.modes.push_back(mode);
                }
                if (!frame_order(x.modes)) return fail(why, "Link modes must be in frame order");
            }
            const auto& data = a.get("data");
            if (data.type != Value::Null) {
                if (data.type != Value::Array || data.array.size() > kMaxEvents) return fail(why, "demo actor data must be a list");
                for (const auto& e : data.array) {
                    Data item;
                    if (!keys_only(e, {"frame", "id", "bytes"}) || !integer(e.get("frame"), 0, d.frames, item.frame) || !integer(e.get("id"), 0, 255, item.id) || !hexbytes(e.get("bytes"), item.bytes, 64))
                        return fail(why, "demo actor data needs frame, id and hex bytes");
                    x.data.push_back(item);
                }
            }
            d.actors.push_back(std::move(x));
        }
    }
    auto find_actor = [&](const std::string& id) { return std::any_of(d.actors.begin(), d.actors.end(), [&](const Actor& a) { return a.id == id; }); };
    const auto& sounds = st.get("sounds");
    if (sounds.type != Value::Null) {
        if (sounds.type != Value::Array || sounds.array.size() > kMaxEvents) return fail(why, "demo sounds must be a list");
        for (const auto& s : sounds.array) {
            Sound x;
            if (!keys_only(s, {"frame", "id", "actor", "position", "volume", "pan"}) || !integer(s.get("frame"), 0, d.frames, x.frame) || !integer(s.get("id"), 1, 0xFFFFFFFE, x.id))
                return fail(why, "demo sound needs frame and id");
            if (s.object.contains("actor") && s.object.contains("position")) return fail(why, "demo sound: actor or position");
            if (s.object.contains("actor")) { x.actor = s.get("actor").string(); if (!find_actor(x.actor)) return fail(why, "demo sound: unknown actor " + x.actor); }
            if (s.object.contains("position")) { Vec p; if (!vec(s.get("position"), p)) return fail(why, "demo sound: invalid position"); x.position = p; }
            if (s.object.contains("volume") && !real(s.get("volume"), 0, 1, x.volume)) return fail(why, "demo sound volume must be 0..1");
            if (s.object.contains("pan") && !real(s.get("pan"), 0, 1, x.pan)) return fail(why, "demo sound pan must be 0..1");
            d.sounds.push_back(x);
        }
    }
    const auto& particles = st.get("particles");
    if (particles.type != Value::Null) {
        if (particles.type != Value::Array || particles.array.size() > kMaxEvents) return fail(why, "demo particles must be a list");
        for (const auto& p : particles.array) {
            Particle x;
            if (!keys_only(p, {"frame", "id", "actor", "joint", "position"}) || !integer(p.get("frame"), 0, d.frames, x.frame) || !integer(p.get("id"), 0, 0xFFFF, x.id))
                return fail(why, "demo particle needs frame and id");
            if (p.object.contains("actor") == p.object.contains("position")) return fail(why, "demo particle: actor or position, exclusively");
            if (p.object.contains("actor")) {
                x.actor = p.get("actor").string();
                if (!find_actor(x.actor)) return fail(why, "demo particle: unknown actor " + x.actor);
                if (p.object.contains("joint")) { x.joint = p.get("joint").string(); if (!plain(x.joint, 31)) return fail(why, "demo particle: invalid joint name"); }
            } else {
                if (p.object.contains("joint")) return fail(why, "demo particle: a joint needs an actor");
                Vec v;
                if (!vec(p.get("position"), v)) return fail(why, "demo particle: invalid position");
                x.position = v;
            }
            d.particles.push_back(x);
        }
    }
    const auto& messages = st.get("messages");
    if (messages.type != Value::Null) {
        if (messages.type != Value::Array || messages.array.size() > 16) return fail(why, "demo messages must be a list of up to 16");
        for (const auto& m : messages.array) {
            Message x;
            if (!keys_only(m, {"frame", "text"}) || !integer(m.get("frame"), 0, d.frames >= 2 ? d.frames - 2 : 0, x.frame) || d.frames < 2 || m.get("text").type != Value::String || m.get("text").text.empty() || m.get("text").text.size() > 400)
                return fail(why, "demo message needs a frame before the end and text");
            x.text = m.get("text").text;
            if (!d.messages.empty() && x.frame <= d.messages.back().frame) return fail(why, "demo message frames must increase");
            d.messages.push_back(x);
        }
    }
    if (d.actors.empty() && d.eye.keys.empty()) return fail(why, "demo needs a camera or actors");
    return true;
}

namespace {
namespace S = demo_stb;
float seconds(uint32_t frames) { return float(frames) / 30.f; }
float component(const Vec& v, unsigned i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
// One curve function for component i of a track (from its first key), or none for a single key.
uint32_t curve(S::Builder& b, const Track& t, unsigned i) {
    const uint32_t f0 = t.keys.front().frame;
    if (t.curve == Curve::Smooth) {
        std::vector<S::HermiteKey> keys;
        for (size_t k = 0; k < t.keys.size(); ++k) {
            const size_t a = k ? k - 1 : k, c = k + 1 < t.keys.size() ? k + 1 : k;
            const float dt = seconds(t.keys[c].frame - t.keys[a].frame);
            const float slope = dt > 0 ? (component(t.keys[c].value, i) - component(t.keys[a].value, i)) / dt : 0;
            keys.push_back({seconds(t.keys[k].frame - f0), component(t.keys[k].value, i), slope, slope});
        }
        return b.hermite(keys);
    }
    std::vector<std::pair<float, float>> keys;
    for (const auto& k : t.keys) keys.push_back({seconds(k.frame - f0), component(k.value, i)});
    return b.list(keys, t.curve == Curve::BSpline ? S::Interpolation::BSpline : S::Interpolation::Linear);
}
// Writes a vector track (3 components, code3 = the XYZ paragraph code) or a scalar one (code1).
void emit(S::Builder& b, size_t o, const Track& t, uint32_t code3, uint32_t code1, bool scalar) {
    if (t.keys.empty()) return;
    auto at = [&](const Key& k) {
        if (scalar) b.immediate(o, k.frame, code1, {k.value.x});
        else b.immediate(o, k.frame, code3, {k.value.x, k.value.y, k.value.z});
    };
    if (t.keys.size() == 1 || t.curve == Curve::Step) { for (const auto& k : t.keys) at(k); return; }
    if (scalar) b.curves(o, t.keys.front().frame, code1, {curve(b, t, 0)});
    else { const uint32_t x = curve(b, t, 0), y = curve(b, t, 1), z = curve(b, t, 2); b.curves(o, t.keys.front().frame, code3, {x, y, z}); }
    at(t.keys.back());  // hold the last value after the curve
}
}  // namespace

bool compile(const Demo& d, const Resolver& resolve, Compiled& out, std::string& why, uint32_t message_number) {
    out = {};
    S::Builder b;
    std::vector<Resource> virtual_table;
    auto virtual_id = [&](const Resource& r, uint32_t& id) {
        auto it = std::find(virtual_table.begin(), virtual_table.end(), r);
        if (it == virtual_table.end()) {
            if (virtual_table.size() >= 64) return fail(why, "demo uses more than 64 archive files");
            const int32_t real = resolve ? resolve(r) : -1;
            if (real < 0) return fail(why, "the game has no " + r.file + " in archive " + r.archive);
            virtual_table.push_back(r);
            out.virtual_ids.push_back({r, uint32_t(real)});
            it = virtual_table.end() - 1;
        }
        id = 0x10000u | uint32_t(it - virtual_table.begin());
        return true;
    };
    if (!d.eye.keys.empty() || !d.fovy.keys.empty() || !d.roll.keys.empty()) {
        const size_t cam = b.object(S::kCamera, "camera");
        emit(b, cam, d.eye, S::camera::Eye, 0, false);
        emit(b, cam, d.target, S::camera::Target, 0, false);
        emit(b, cam, d.fovy, 0, S::camera::Fovy, true);
        emit(b, cam, d.roll, 0, S::camera::Roll, true);
        b.end(cam, d.frames);
    }
    std::map<std::string, std::pair<size_t, const Actor*>> objects;  // demo id -> builder object
    unsigned models = 0;
    for (const auto& a : d.actors) {
        std::string name = a.actor;
        if (a.kind == Actor::Kind::Model) name = "d_act" + std::to_string(models++);
        else out.participants.push_back(a.actor);
        const size_t o = b.object(S::kActor, name);
        objects[a.id] = {o, &a};
        if (a.kind == Actor::Kind::Model) {
            uint32_t shape;
            if (!virtual_id(a.model, shape)) return false;
            b.value(o, 0, S::actor::Shape, shape);
        }
        emit(b, o, a.position, S::actor::TXYZ, 0, false);
        emit(b, o, a.rotation, S::actor::RXYZ, 0, false);
        emit(b, o, a.scale, S::actor::SXYZ, 0, false);
        for (const auto& c : a.clips) {
            uint32_t id;
            if (!virtual_id(c.resource, id)) return false;
            b.value(o, c.frame, S::actor::Animation, id);
            b.immediate_word(o, c.frame, S::actor::AnimationMode, 1);
            b.time(o, c.frame, S::actor::AnimationFrame, 30.f * c.speed);
        }
        for (const auto& m : a.modes) {
            b.value(o, m.frame, S::actor::Animation, m.mode);
            if (!m.data.empty()) {
                b.immediate_word(o, m.frame, S::actor::AnimationMode, 1);
                b.time(o, m.frame, S::actor::AnimationFrame, 30.f);
                b.data(o, m.frame, 1, m.data);
            }
        }
        for (const auto& e : a.data) b.data(o, e.frame, e.id, e.bytes);
        b.end(o, d.frames);
    }
    unsigned n = 0;
    for (const auto& s : d.sounds) {
        const size_t o = b.object(S::kSound, "se" + std::to_string(n++));
        if (!s.actor.empty()) {
            const Actor* a = objects[s.actor].second;
            Track t = a->position;
            // the sound follows the actor's own position track from the sound's frame on
            if (!t.keys.empty()) emit(b, o, t, S::sound::XYZ, 0, false);
        } else if (s.position) b.immediate(o, s.frame, S::sound::XYZ, {s.position->x, s.position->y, s.position->z});
        b.value(o, s.frame, S::sound::Sound, s.id);
        b.immediate(o, s.frame, S::sound::Volume, {s.volume});
        b.immediate(o, s.frame, S::sound::Pan, {s.pan});
        b.end(o, d.frames);
    }
    n = 0;
    for (const auto& p : d.particles) {
        const size_t o = b.object(S::kParticle, "efx" + std::to_string(n++));
        if (!p.actor.empty()) {
            const Actor* a = objects[p.actor].second;
            const std::string target = a->kind == Actor::Kind::Model ? "d_act" + std::to_string(std::count_if(d.actors.begin(), d.actors.begin() + (a - d.actors.data()), [](const Actor& x) { return x.kind == Actor::Kind::Model; })) : a->actor;
            b.name(o, p.frame, S::particle::Parent, target);
            if (!p.joint.empty()) b.name(o, p.frame, S::particle::ParentNode, p.joint);
            b.immediate_word(o, p.frame, S::particle::ParentEnable, 1);
        } else b.immediate(o, p.frame, S::particle::TXYZ, {p.position->x, p.position->y, p.position->z});
        b.value(o, p.frame, S::particle::Particle, p.id);
        b.end(o, d.frames);
    }
    if (!d.messages.empty()) {
        const size_t o = b.object(S::kMessage, "message");
        for (const auto& m : d.messages) { b.value(o, m.frame, S::message::Message, message_number); b.suspend(m.frame + 1); out.messages.push_back(m); }
        b.end(o, d.frames);
    }
    out.stb = b.finish();
    if (out.stb.size() > kMaxStbBytes) return fail(why, "demo data exceeds 48 KiB");
    return true;
}
}  // namespace demo_scene
