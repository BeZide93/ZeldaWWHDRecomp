// Cloud research prototype, Metal side (cloud_proto.cpp has the game side; off by default).
//
// WWHD_CLOUDS_PROTO=sdf     each sky cloud (vrkumo) of the game becomes a toon-shaded SDF cumulus:
//                           a camera-facing impostor quad per cloud, raymarched against a few
//                           smooth-unioned spheres with a flat base, displaced by a baked tiling 3D
//                           noise; 3 hard light bands from the game's sun and cloud palette, a cool
//                           underside, a sun-side rim and a noise-frayed fringe.
// WWHD_CLOUDS_PROTO=volume  one full-screen pass: a raymarched cloud slab whose coverage comes from the
//                           same game clouds, with posterised (banded) light (option D, for comparison)
// WWHD_CLOUDS_PROTO=hide    only skip the game's cards (A/B reference)
// WWHD_CLOUDS_PROTO=mark    keep the game's cards (timing reference with WWHD_CLOUDS_TIME)
// WWHD_CLOUDS_TIME=1        draws the clouds in their own command buffer and logs its GPU time
// WWHD_CLOUDS_REPEAT=n      draws the pass n times (timing only)
// WWHD_CLOUDS_LOG=1         logs the snapshot now and then (calibration)
// WWHD_SKY_GRADE=x          (sdf/volume/hide) 0..1: deepens the sky behind the clouds (research only)
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <atomic>
#include <string>
#include <unordered_map>

#include "cloud_proto.h"
#include "renderer.h"
#include "metal.h"
#include "runtime.h"

namespace gfx {

static const char* kCloudShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Globals {
    float3x3 view;      // world (camera-relative) -> camera
    float4 proj;        // f/aspect, f, aspect, w: 1 = combined mode (D draws only the far banks)
    float4 sun;         // xyz: light direction (sun, or the moon at night), w: daylight 0..1
    float4 lit;         // rgb: palette lit colour (normalised), w: warmth (low sun) 0..1
    float4 sky;         // rgb: palette sky/fog colour, w: storm strength 0..1
    float4 params;      // x: band softness, y: noise strength, z: haze reduction, w: self-shadow steps
    float4 tune;        // x: fringe width, y: palette saturation, z: shadow level, w: mid level
    float4 extra;       // x: max march steps, y: 1 = cheap normals, z: clip-space depth, w: instance count
};
struct Inst {
    float4 center;      // xyz: cloud base centre (camera-relative world), w: width radius
    float4 shape;       // x: height/width, y: seed, z: alpha, w: distance blend (1 - falloff)
    float4 base;        // rgb: lit colour of this cloud, w: 1 = far (horizon bank)
};
struct VOut {
    float4 pos [[position]];
    float3 world;
    uint inst [[flat]];
};
struct FOut { float4 c [[color(0)]]; };

static float hash1(float n) { return fract(sin(n) * 43758.5453); }
static float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// ---- shared toon shading: 3 hard bands, warm lit / cool shadow with a height-gradient ambient
// (sky above, sea bounce below: Cumulus' ambient idea), sun-side rim and a forward-scatter silver lining
struct Shade { float3 lit, mid, shadow, rim; };
static Shade palette(constant Globals& g, float3 base, float far) {
    Shade s;
    float storm = g.sky.w, warm = g.lit.w;
    float luma = dot(base, float3(0.3, 0.59, 0.11));
    // whiter at noon, the palette's own colour (gold, pink) when the sun is low
    float sat = mix(g.tune.y, 1.0, warm);
    float3 lit = mix(float3(luma), base, sat);
    // full brightness by day; at night the palette's own (dark) level
    float mx = max(max(base.r, max(base.g, base.b)), 1e-3);
    lit = lit / max(max(lit.r, max(lit.g, lit.b)), 1e-3) * mix(min(mx * 1.15, 1.0), 1.0, g.sun.w);
    lit *= 1.0 - 0.45 * storm;
    float3 skyAmb = mix(g.sky.rgb, float3(1.0), 0.42 * g.sun.w);  // no white ambient at night
    s.lit = lit;
    s.shadow = mix(lit * g.tune.z, skyAmb * mix(0.95, 0.55, storm), 0.48);
    s.mid = mix(lit * g.tune.w, skyAmb, 0.10);
    // low sun: golden-pink lit side, lavender shadows (warm/cool contrast)
    s.lit = mix(s.lit, s.lit * float3(1.0, 0.82, 0.62) + float3(0.0, 0.02, 0.04), warm * 0.8);
    s.shadow = mix(s.shadow, s.shadow * float3(0.80, 0.66, 0.92), warm);
    s.mid = mix(s.mid, s.mid * float3(0.95, 0.80, 0.82), warm * 0.5);
    s.mid = mix(s.mid, s.mid * float3(1.0, 0.86, 0.80), warm * 0.8);
    s.rim = min(mix(lit * 1.08 + 0.04, float3(1.0, 0.86, 0.55), warm), float3(1.0));
    float3 haze = mix(g.sky.rgb, float3(1.0), 0.55 * g.sun.w);
    float f = far * 0.32;
    s.lit = mix(s.lit, haze, f), s.mid = mix(s.mid, haze, f), s.shadow = mix(s.shadow, haze, f), s.rim = mix(s.rim, haze, f * 0.5);
    return s;
}
static float3 toon(constant Globals& g, Shade c, float3 n, float3 nDetail, float3 rd, float height, float shadowTerm) {
    float3 L = g.sun.xyz;
    float ndl = dot(n, L) * 0.5 + 0.5;
    float hw = 0.4 * (1.0 - 0.75 * g.lit.w);  // a low sun lights the sides it faces, not every top
    float lightv = (ndl * (1.0 - hw) + height * hw) * mix(0.45, 1.0, shadowTerm);
    float sw = g.params.x;
    float3 col = mix(c.shadow, c.mid, smoothstep(0.26 - sw, 0.26 + sw, lightv));
    col = mix(col, c.lit, smoothstep(0.40 - sw, 0.40 + sw, lightv));
    // sea bounce under the base: cooler, a little lighter than the side shadow
    float down = smoothstep(-0.3, -0.8, n.y);
    col = mix(col, c.shadow * float3(0.94, 1.0, 1.06), down * 0.7);
    // rim and silver lining: silhouette x light side; much stronger looking toward a low sun
    // silhouette from the smooth normal (outer edges only), nudged by the detail normal for a frayed line
    float facing = saturate(dot(normalize(mix(n, nDetail, 0.25)), -rd));
    float toward = saturate(dot(rd, L));
    float fwd = 0.5 + 3.0 * pow(toward, 2.0) * (0.3 + 0.7 * g.lit.w);
    float rim = pow(1.0 - facing, 2.5) * saturate(dot(n, L) + 0.55) * fwd * g.sun.w;
    col = mix(col, c.rim, smoothstep(0.32 - sw, 0.32 + sw, rim));
    return col;
}

// ---- C: one impostor per cloud, raymarched against smooth-unioned spheres
vertex VOut cl_vs(uint vid [[vertex_id]], uint iid [[instance_id]], constant Globals& g [[buffer(0)]],
                  const device Inst* inst [[buffer(1)]]) {
    Inst c = inst[iid];
    float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
    float r = c.center.w * max(1.0, c.shape.x) * 1.6;
    float3 mid = c.center.xyz + float3(0, c.center.w * c.shape.x * 0.6, 0);
    float3 right = float3(g.view[0][0], g.view[1][0], g.view[2][0]);
    float3 up = float3(g.view[0][1], g.view[1][1], g.view[2][1]);
    float3 p = mid + (corner.x * right + corner.y * up) * r;
    float3 q = g.view * p;
    VOut o;
    float w = -q.z;
    o.pos = float4(q.x * g.proj.x, q.y * g.proj.y, g.extra.z * w, w);
    o.world = p;
    o.inst = iid;
    return o;
}
static float blobs(float3 p, float hs, float seed) {
    float d = 1e9;
    for (int i = 0; i < 4; i++) {  // base row
        float fi = float(i);
        float x = -0.85 + fi * 0.57 + (hash1(seed + fi) - 0.5) * 0.25;
        float r = 0.42 + hash1(seed + fi + 7.0) * 0.16;
        float z = (hash1(seed + fi + 13.0) - 0.5) * 0.35;
        d = smin(d, length(p - float3(x, r * 0.75 * hs, z)) - r, 0.12);
    }
    for (int i = 0; i < 3; i++) {  // middle
        float fi = float(i);
        float x = -0.5 + fi * 0.5 + (hash1(seed + fi + 21.0) - 0.5) * 0.3;
        float r = 0.36 + hash1(seed + fi + 29.0) * 0.18;
        float y = (0.55 + hash1(seed + fi + 37.0) * 0.25) * hs;
        d = smin(d, length(p - float3(x, y, (hash1(seed + fi + 41.0) - 0.5) * 0.3)) - r, 0.1);
    }
    for (int i = 0; i < 2; i++) {  // crown
        float fi = float(i);
        float cx = (hash1(seed + 53.0 + fi) - 0.5) * 0.7;
        d = smin(d, length(p - float3(cx, (0.95 + hash1(seed + 59.0 + fi) * 0.25) * hs, 0.0)) -
                    (0.26 + hash1(seed + 61.0 + fi) * 0.12), 0.1);
    }
    return max(d, -(p.y - 0.06));  // flat base
}
static float sdf(float3 p, float hs, float seed, texture3d<float> noise, sampler s, float amp, float ero) {
    float d = blobs(p, hs, seed) + ero;
    if (d > 0.35) return d;
    float3 np = p * 0.8 + float3(seed * 0.37, 0.0, seed * 0.11);
    float n = noise.sample(s, np).g * 0.6 + noise.sample(s, np * 2.7).r * 0.4;
    return d + (0.5 - n) * amp;
}
fragment FOut cl_fs(VOut in [[stage_in]], constant Globals& g [[buffer(0)]], const device Inst* inst [[buffer(1)]],
                    texture3d<float> noise [[texture(0)]], sampler s [[sampler(0)]]) {
    Inst c = inst[in.inst];
    float hs = c.shape.x, seed = c.shape.y, amp = g.params.y;
    float ero = (1.0 - saturate(c.shape.z * 1.25)) * 0.55;  // the game's fade erodes the shape
    float3 rd = normalize(in.world);
    float3 ro = -c.center.xyz / c.center.w;
    float3 bc = float3(0, 0.6 * hs, 0);
    float br = 1.5 * max(1.0, hs);
    float3 oc = ro - bc;
    float b = dot(oc, rd), cc = dot(oc, oc) - br * br, h = b * b - cc;
    if (h < 0.0) discard_fragment();
    h = sqrt(h);
    float t = max(-b - h, 0.0), t1 = -b + h;
    float dmin = 1e9, tmin = t;
    bool hit = false;
    float3 p;
    float eps = 0.0015 * t;
    for (int i = 0; i < int(g.extra.x) && t < t1; i++) {
        p = ro + rd * t;
        float d = sdf(p, hs, seed, noise, s, amp, ero);
        if (d < dmin) { dmin = d; tmin = t; }
        if (d < eps) { hit = true; break; }
        t += max(d * 0.8, 0.008);
    }
    Shade pal = palette(g, c.base.rgb, c.shape.w);
    FOut o;
    if (!hit) {  // frayed fringe just outside the surface
        float fw = g.tune.x;
        if (dmin > fw) discard_fragment();
        float3 q = ro + rd * tmin;
        float n = noise.sample(s, q * 1.9 + seed).g;
        float a = (1.0 - dmin / fw) * smoothstep(0.4, 0.7, n);
        if (a < 0.05) discard_fragment();
        a = saturate(a * 2.0) * saturate(c.shape.z * 4.0);
        float3 col = mix(pal.mid, pal.lit, saturate(q.y / (1.2 * hs)));
        o.c = float4(col * a, a);
        return o;
    }
    float3 nb = normalize(float3( 1, -1, -1) * blobs(p + float3( 1, -1, -1) * 0.06, hs, seed) +
                          float3(-1, -1,  1) * blobs(p + float3(-1, -1,  1) * 0.06, hs, seed) +
                          float3(-1,  1, -1) * blobs(p + float3(-1,  1, -1) * 0.06, hs, seed) +
                          float3( 1,  1,  1) * blobs(p + float3( 1,  1,  1) * 0.06, hs, seed));
    float3 nd = nb;
    if (g.extra.y < 0.5) {
        const float e = 0.01;
        nd = normalize(float3( 1, -1, -1) * sdf(p + float3( 1, -1, -1) * e, hs, seed, noise, s, amp, ero) +
                       float3(-1, -1,  1) * sdf(p + float3(-1, -1,  1) * e, hs, seed, noise, s, amp, ero) +
                       float3(-1,  1, -1) * sdf(p + float3(-1,  1, -1) * e, hs, seed, noise, s, amp, ero) +
                       float3( 1,  1,  1) * sdf(p + float3( 1,  1,  1) * e, hs, seed, noise, s, amp, ero));
    }
    float3 n = normalize(mix(nb, nd, 0.15));
    float sh = 1.0;
    if (g.params.w > 0.0) {  // self-shadow: soft-shadow march toward the light through the blob field
        float ts = 0.06;
        for (int i = 0; i < int(g.params.w); i++) {
            float dd = blobs(p + g.sun.xyz * ts, hs, seed);
            sh = min(sh, saturate(6.0 * dd / ts + 0.5));
            ts += max(dd, 0.08);
        }
    }
    float3 col = toon(g, pal, n, nd, rd, saturate(p.y / (1.1 * hs)), sh);
    float a = saturate(c.shape.z * 6.0);
    o.c = float4(col * a, a);
    return o;
}

// ---- D: one full-screen pass over a merged cloud field. Each game cloud is a flat-based, dome-topped
// coverage blob; neighbouring blobs merge into banks; noise erodes billows; the first crossing of a
// density threshold is the toon surface (hard edge), shaded like C, with a density march toward the sun
// for self-shadowing.
struct FVOut { float4 pos [[position]]; float2 ndc; };
vertex FVOut vol_vs(uint vid [[vertex_id]], constant Globals& g [[buffer(0)]]) {
    float2 p = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
    FVOut o; o.pos = float4(p, g.extra.z, 1); o.ndc = p; return o;
}
struct Cand { int k; float t0, t1; };
constant float g_dfreq = 0.00042;
static float field(float3 p, const device Inst* inst, thread const Cand* cand, int nc, texture3d<float> noise, sampler s,
                   float amp, thread float& hgt, thread int& who, bool detail = true) {
    float cov = 0.0, rr = 1000.0;
    hgt = 0.0;
    for (int j = 0; j < nc; j++) {
        Inst c = inst[cand[j].k];
        float R = c.center.w * 1.7, H = c.center.w * c.shape.x * 1.9;
        float2 dxz = (p.xz - c.center.xz) / R;
        float fh = 1.0 - dot(dxz, dxz);
        if (fh <= 0.0) continue;
        float v = (p.y - c.center.y) / H;
        float prof = saturate(v * 6.0) * saturate(1.0 - v * v / max(fh, 0.05));
        float a = saturate(c.shape.z * 1.3) * prof * sqrt(fh);
        if (a > cov) { cov = a; hgt = saturate(v); who = cand[j].k; rr = R; }
    }
    if (cov <= 0.0) return -1.0;
    if (!detail) return cov - 0.30;
    float3 np = p / (rr * 0.85);
    float n = noise.sample(s, np).g * 0.6 + noise.sample(s, np * 2.9).r * 0.4;
    return cov - 0.30 - (0.55 - n) * amp * 1.25;
}
fragment FOut vol_fs(FVOut in [[stage_in]], constant Globals& g [[buffer(0)]], const device Inst* inst [[buffer(1)]],
                     texture3d<float> noise [[texture(0)]], sampler s [[sampler(0)]]) {
    float3 dc = normalize(float3(in.ndc.x / g.proj.x, in.ndc.y / g.proj.y, -1.0));
    float3 rd = transpose(g.view) * dc;
    // candidate clouds: ray vs each blob's vertical cylinder (up to 8, nearest first)
    Cand cand[8];
    int nc = 0;
    int count = int(g.extra.w);
    for (int k = 0; k < count; k++) {
        Inst c = inst[k];
        if (c.base.w < 0.5 && g.proj.w > 0.5) continue;  // combined mode: D draws only the far banks
        float R = c.center.w * 1.7, H = c.center.w * c.shape.x * 1.9;
        float2 o2 = -c.center.xz, d2 = rd.xz;
        float A = dot(d2, d2), B = dot(o2, d2), C = dot(o2, o2) - R * R;
        float disc = B * B - A * C;
        if (disc <= 0.0 || A < 1e-8) continue;
        disc = sqrt(disc);
        float t0 = (-B - disc) / A, t1 = (-B + disc) / A;
        // clip to the blob's height range
        if (abs(rd.y) > 1e-5) {
            float ya = (c.center.y - rd.y * 0.0) / rd.y, yb = (c.center.y + H) / rd.y;
            float lo = min(ya, yb), hi = max(ya, yb);
            t0 = max(t0, lo), t1 = min(t1, hi);
        } else if (c.center.y > 0.0 || c.center.y + H < 0.0) continue;
        t0 = max(t0, 0.0);
        if (t1 <= t0) continue;
        Cand nw = {k, t0, t1};
        int at = nc < 8 ? nc : 8;
        for (int j = 0; j < min(nc, 8); j++) if (t0 < cand[j].t0) { at = j; break; }
        if (at >= 8) continue;
        for (int j = min(nc, 7); j > at; j--) cand[j] = cand[j - 1];
        cand[at] = nw;
        nc = min(nc + 1, 8);
    }
    if (nc == 0) discard_fragment();
    float amp = g.params.y;
    float hgt; int who = cand[0].k;
    bool hit = false;
    float3 p;
    int steps = int(g.extra.x);
    for (int j = 0; j < nc && !hit; j++) {
        float dt = (cand[j].t1 - cand[j].t0) / float(steps);
        for (int i = 0; i <= steps; i++) {
            float t = cand[j].t0 + dt * float(i);
            float3 q = rd * t;
            float f = field(q, inst, cand, nc, noise, s, amp, hgt, who);
            if (f > 0.0) {
                // refine the crossing
                float a = i > 0 ? t - dt : t, b2 = t;
                for (int r = 0; r < 5; r++) {
                    float m = 0.5 * (a + b2);
                    if (field(rd * m, inst, cand, nc, noise, s, amp, hgt, who) > 0.0) b2 = m; else a = m;
                }
                p = rd * b2;
                hit = true;
                break;
            }
        }
    }
    if (!hit) discard_fragment();
    Inst c = inst[who];
    float hh; int ww;
    float e = c.center.w * 0.04;
    float f0 = field(p, inst, cand, nc, noise, s, amp, hh, ww);
    float3 grad = float3(field(p + float3(e, 0, 0), inst, cand, nc, noise, s, amp, hh, ww) - f0,
                         field(p + float3(0, e, 0), inst, cand, nc, noise, s, amp, hh, ww) - f0,
                         field(p + float3(0, 0, e), inst, cand, nc, noise, s, amp, hh, ww) - f0);
    float3 nd = normalize(-grad + float3(0, 1e-6, 0));
    // proxy normal from the smooth coverage field (clean toon bands), detail normal for the rim
    float e2 = c.center.w * 0.15;
    float g0 = field(p, inst, cand, nc, noise, s, amp, hh, ww, false);
    float3 gradC = float3(field(p + float3(e2, 0, 0), inst, cand, nc, noise, s, amp, hh, ww, false) - g0,
                          field(p + float3(0, e2, 0), inst, cand, nc, noise, s, amp, hh, ww, false) - g0,
                          field(p + float3(0, 0, e2), inst, cand, nc, noise, s, amp, hh, ww, false) - g0);
    float3 nc2 = normalize(-gradC + float3(0, 1e-6, 0));
    float3 n = normalize(mix(nc2, nd, 0.3));
    float sh = 1.0;
    if (g.params.w > 0.0) {
        float acc = 0.0;
        for (int i = 1; i <= int(g.params.w); i++) {
            float fs = field(p + g.sun.xyz * c.center.w * 0.12 * float(i), inst, cand, nc, noise, s, amp, hh, ww);
            acc += saturate(fs * 4.0);
        }
        sh = 1.0 - saturate(acc / max(g.params.w, 1.0) * 1.6);
    }
    Shade pal = palette(g, c.base.rgb, c.shape.w);
    float3 col = toon(g, pal, n, nd, rd, hgt, sh);
    float a = saturate(c.shape.z * 6.0);
    FOut o;
    o.c = float4(col * a, a);
    return o;
}

// ---- vivid sky: pull the horizon haze band (kasumi) back toward the sky colour (sky buffer, before clouds)
fragment FOut grade_fs(FVOut in [[stage_in]], constant Globals& g [[buffer(0)]]) {
    float3 dc = normalize(float3(in.ndc.x / g.proj.x, in.ndc.y / g.proj.y, -1.0));
    float3 rd = transpose(g.view) * dc;
    float band = (1.0 - smoothstep(0.0, 0.32, rd.y)) * step(-0.02, rd.y);
    float3 sky = g.sky.rgb * mix(0.95, 1.12, saturate(rd.y * 4.0));
    // keep a warm glow around a low sun
    float aroundSun = pow(saturate(dot(rd, g.sun.xyz)), 6.0) * g.lit.w;
    float a = saturate(g.params.z) * band * (1.0 - aroundSun);
    FOut o;
    o.c = float4(sky * a, a);
    return o;
}
)";

// a tiling 64^3 RG8 noise: R value-noise fbm, G inverted Worley (cells of 8) - generated here
static id<MTLTexture> make_noise() {
    const int N = 64;
    std::vector<uint8_t> data(size_t(N) * N * N * 2);
    auto hash = [](uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
        return x;
    };
    auto lattice = [&](int x, int y, int z, int period, uint32_t salt) {
        x = (x % period + period) % period, y = (y % period + period) % period, z = (z % period + period) % period;
        return (hash((uint32_t)(x + period * (y + period * z)) * 2654435761u + salt) & 0xFFFF) / 65535.0f;
    };
    auto vnoise = [&](float x, float y, float z, int period, uint32_t salt) {
        int ix = (int)std::floor(x), iy = (int)std::floor(y), iz = (int)std::floor(z);
        float fx = x - ix, fy = y - iy, fz = z - iz;
        auto sm = [](float t) { return t * t * (3 - 2 * t); };
        fx = sm(fx), fy = sm(fy), fz = sm(fz);
        float v = 0;
        for (int c = 0; c < 8; c++) {
            int dx = c & 1, dy = (c >> 1) & 1, dz = c >> 2;
            float w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
            v += w * lattice(ix + dx, iy + dy, iz + dz, period, salt);
        }
        return v;
    };
    const int cells = 8;
    std::vector<float> fp(cells * cells * cells * 3);
    for (int i = 0; i < cells * cells * cells; i++)
        for (int k = 0; k < 3; k++) fp[i * 3 + k] = (hash(i * 3 + k + 977) & 0xFFFF) / 65535.0f;
    for (int z = 0; z < N; z++)
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                float f = 0, amp = 0.5f, norm = 0;
                for (int o = 0, per = 4; o < 4; o++, per *= 2, amp *= 0.5f) {
                    float s = (float)per / N;
                    f += amp * vnoise(x * s, y * s, z * s, per, 31u * o);
                    norm += amp;
                }
                f /= norm;
                // Worley: distance to the nearest feature point (tiling)
                float cs = (float)N / cells, best = 9;
                int cx = (int)(x / cs), cy = (int)(y / cs), cz = (int)(z / cs);
                for (int dz = -1; dz <= 1; dz++)
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int ox = cx + dx, oy = cy + dy, oz = cz + dz;
                            int wx = (ox + cells) % cells, wy = (oy + cells) % cells, wz = (oz + cells) % cells;
                            const float* q = &fp[(wx + cells * (wy + cells * wz)) * 3];
                            float px = (ox + q[0]) * cs, py = (oy + q[1]) * cs, pz = (oz + q[2]) * cs;
                            float ddx = px - x, ddy = py - y, ddz = pz - z;
                            best = std::min(best, (ddx * ddx + ddy * ddy + ddz * ddz) / (cs * cs));
                        }
                float wv = 1.0f - std::min(1.0f, std::sqrt(best));
                size_t i = (size_t(z) * N * N + size_t(y) * N + x) * 2;
                data[i] = (uint8_t)std::lround(std::clamp(f, 0.0f, 1.0f) * 255);
                data[i + 1] = (uint8_t)std::lround(wv * 255);
            }
    MTLTextureDescriptor* d = [MTLTextureDescriptor new];
    d.textureType = MTLTextureType3D;
    d.pixelFormat = MTLPixelFormatRG8Unorm;
    d.width = d.height = d.depth = N;
    d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [R.device newTextureWithDescriptor:d];
    [t replaceRegion:MTLRegionMake3D(0, 0, 0, N, N, N) mipmapLevel:0 slice:0 withBytes:data.data()
         bytesPerRow:N * 2 bytesPerImage:N * N * 2];
    return t;
}

struct Globals {
    float view[12];  // float3x3: 3 columns of float4 (padded)
    float proj[4], sun[4], lit[4], sky[4], params[4], tune[4], extra[4];  // as the shader's Globals
};
struct Inst {
    float center[4], shape[4], base[4];
};

static float envf(const char* n, float d) { const char* e = getenv(n); return e && *e ? (float)atof(e) : d; }
static bool env_on(const char* n) { const char* e = getenv(n); return e && *e && strcmp(e, "0"); }

// draws the clouds of snapshot `seq` into the open encoder's pass; vp: the game's viewport
enum { kGrade = 1, kClouds = 2 };
static void encode(id<MTLRenderCommandEncoder> enc, const cloud_proto::Snapshot& s, const MTLViewport& vp, int what,
                   id<MTLDepthStencilState> depthState = nil) {
    static id<MTLLibrary> lib;
    static id<MTLTexture> noise;
    static id<MTLSamplerState> samp;
    static id<MTLDepthStencilState> noDepth;
    static std::unordered_map<std::string, id<MTLRenderPipelineState>> pipes;
    if (!lib) {
        NSError* err = nil;
        lib = [R.device newLibraryWithSource:[NSString stringWithUTF8String:kCloudShader] options:nil error:&err];
        if (!lib) { LOG("[clouds] shader: %s", err.localizedDescription.UTF8String); return; }
        noise = make_noise();
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = sd.tAddressMode = sd.rAddressMode = MTLSamplerAddressModeRepeat;
        samp = [R.device newSamplerStateWithDescriptor:sd];
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        dd.depthWriteEnabled = NO;
        noDepth = [R.device newDepthStencilStateWithDescriptor:dd];
        LOG("[clouds] prototype shaders ready (mode %d)", cloud_proto::mode());
    }
    if (!(what & kClouds) && render::horizon_haze() >= 1.0f) return;  // nothing to draw: leave the encoder alone
    const int mode = cloud_proto::mode();
    // pipeline for the open pass's attachment formats
    auto pipe = [&](NSString* vsName, NSString* fsName) -> id<MTLRenderPipelineState> {
        std::string key = std::string(vsName.UTF8String) + fsName.UTF8String;
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [lib newFunctionWithName:vsName];
        d.fragmentFunction = [lib newFunctionWithName:fsName];
        for (int i = 0; i < 8; i++) {
            if (!R.passColor[i]) continue;
            MTLPixelFormat f = R.passColor[i]->tex.pixelFormat;
            d.colorAttachments[i].pixelFormat = f;
            d.rasterSampleCount = R.passColor[i]->tex.sampleCount;
            key += "/" + std::to_string((int)f);
            if (i == 0) {
                auto* a = d.colorAttachments[0];
                a.blendingEnabled = YES;
                a.sourceRGBBlendFactor = MTLBlendFactorOne;
                a.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                a.sourceAlphaBlendFactor = MTLBlendFactorZero;
                a.destinationAlphaBlendFactor = MTLBlendFactorOne;
            } else {
                d.colorAttachments[i].writeMask = MTLColorWriteMaskNone;
            }
        }
        if (R.passDepth) {
            d.depthAttachmentPixelFormat = R.passDepth->tex.pixelFormat;
            if (R.passDepth->fmt.stencil) d.stencilAttachmentPixelFormat = R.passDepth->tex.pixelFormat;
            key += "/d" + std::to_string((int)R.passDepth->tex.pixelFormat);
        }
        auto it = pipes.find(key);
        if (it != pipes.end()) return it->second;
        NSError* err = nil;
        id<MTLRenderPipelineState> p = [R.device newRenderPipelineStateWithDescriptor:d error:&err];
        if (!p) LOG("[clouds] pipeline %s: %s", key.c_str(), err.localizedDescription.UTF8String);
        else LOG("[clouds] pipeline %s", key.c_str());
        pipes[key] = p;
        return p;
    };

    Globals g{};
    // view rows (world -> camera) into a float3x3 with columns c: m[c][r] = row r, column c
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) g.view[c * 4 + r] = s.viewRot[r * 4 + c];
    float aspect = (float)(vp.width / std::fabs(vp.height));
    float f = 1.0f / std::tan(s.fovy * 0.5f * 3.14159265f / 180.0f);
    static const bool logOn = env_on("WWHD_CLOUDS_LOG");
    g.proj[0] = f / aspect, g.proj[1] = f, g.proj[2] = aspect, g.proj[3] = mode == cloud_proto::kCombo ? 1.0f : 0.0f;
    // light: the sun; below the horizon the moon (dimmer, no rim); never from below
    auto dir = [&](const float* pos, float* out) {
        float d[3] = {pos[0] - s.eye[0], pos[1] - s.eye[1], pos[2] - s.eye[2]};
        float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len < 1e-3f) { out[0] = 0, out[1] = 1, out[2] = 0; return; }
        for (int i = 0; i < 3; i++) out[i] = d[i] / len;
    };
    float L[3] = {0, 1, 0};
    if (s.sunVisible) dir(s.sunPos, L);
    float elev = L[1];
    float day = std::clamp(elev * 5.0f + 0.3f, 0.0f, 1.0f);
    if (elev < 0.02f) {
        float M[3];
        dir(s.moonPos, M);
        if (M[1] > 0.05f) for (int i = 0; i < 3; i++) L[i] = M[i];
        else L[0] *= 0.3f, L[2] *= 0.3f, L[1] = 0.9f;
    }
    if (L[1] < 0.12f) {  // keep the light a little above the horizon so tops stay lit
        L[1] = 0.12f;
        float len = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
        for (float& v : L) v /= len;
    }
    for (int i = 0; i < 3; i++) g.sun[i] = L[i];
    g.sun[3] = day;
    static const float strengthOverride = envf("WWHD_CLOUDS_STRENGTH", -1.0f);  // research: visual storm
    const float strength = strengthOverride >= 0 ? strengthOverride : s.strength;
    // warmth from the clock (the game's sun stays fairly high at dusk): dawn around 90, dusk around 270
    // (360 = a day); WWHD_CLOUDS_WARM overrides
    auto bump = [](float t, float c, float w) { return std::max(0.0f, 1.0f - std::fabs(t - c) / w); };
    float warm = std::clamp(bump(s.curTime, 268.0f, 28.0f) + bump(s.curTime, 92.0f, 22.0f), 0.0f, 1.0f);
    warm = std::max(warm, std::clamp(1.0f - (elev - 0.05f) * 3.0f, 0.0f, 1.0f) * day);
    static const float warmOverride = envf("WWHD_CLOUDS_WARM", -1.0f);
    if (warmOverride >= 0) warm = warmOverride;
    for (int i = 0; i < 3; i++) g.sky[i] = s.fogColor[i];
    g.sky[3] = strength;
    g.lit[3] = warm;
    const float haze = 1.0f - render::horizon_haze();  // 0 = the game's haze .. 1 = horizon haze band removed
    g.params[0] = envf("WWHD_CLOUDS_BAND", 0.010f), g.params[1] = envf("WWHD_CLOUDS_NOISE", 0.38f), g.params[2] = haze;
    g.params[3] = envf("WWHD_CLOUDS_SELFSHADOW", 6.0f);
    g.tune[0] = envf("WWHD_CLOUDS_FRINGE", 0.02f), g.tune[1] = envf("WWHD_CLOUDS_SAT", 0.25f);
    g.tune[2] = envf("WWHD_CLOUDS_SHADOW", 0.72f), g.tune[3] = envf("WWHD_CLOUDS_MID", 0.92f);
    g.extra[0] = envf("WWHD_CLOUDS_STEPS", mode == cloud_proto::kSdf ? 48.0f : 24.0f), g.extra[1] = envf("WWHD_CLOUDS_CHEAP", 0.0f);
    g.extra[2] = depthState ? envf("WWHD_CLOUDS_Z", 0.99999f) : 0.5f;

    // instances: one per game cloud (drawVrkumo's layer-0 size), plus, for distant clouds near the
    // horizon, satellites that turn them into banks; the game's placement, drift, fade and weather drive all
    std::vector<Inst> inst;
    std::vector<float> dist;
    const float* k = s.k;  // sizeBase, sizeGain, variation, pitchScale, sideGain, pitchGain, starSizeBase, farPlane
    static const float sizeK = envf("WWHD_CLOUDS_SIZE", 1.35f), hsK = envf("WWHD_CLOUDS_HS", 0.75f);
    static const float bankK = envf("WWHD_CLOUDS_BANKS", 1.0f), bankDist = envf("WWHD_CLOUDS_BANK_DIST", 10500.0f);
    auto h01 = [](uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
        return (x & 0xFFFF) / 65535.0f;
    };
    for (int n = 0; n < s.n; n++) {
        const cloud_proto::Cloud& c = s.c[n];
        float x = c.pos[0], y = c.pos[1], z = c.pos[2];
        float hor = std::sqrt(x * x + z * z), D = std::sqrt(hor * hor + y * y);
        if (D < 1) continue;
        float v = (float)(c.index & 15) * k[2];
        float shape = 1 - v * v * v;
        float weather = k[0] * strength + k[1];
        float ang = shape * weather * c.falloff;
        float pitch = std::atan2(y, hor);
        float q = std::min(pitch / k[3], 1.0f), qc = q * q * q;
        float side = ang * k[0] * (1 + qc * k[4]);
        float tall = ang * (1 + c.height) * k[6] * (1 + qc * k[5]);
        // overhead clouds: keep them from filling the sky (the game's cards grow toward the zenith)
        static const float maxAng = envf("WWHD_CLOUDS_MAXANG", 0.16f);
        float rw = D * std::tan(std::min(side, 1.2f)) * sizeK;
        rw = std::min(rw, D * maxAng) * (1.0f - 0.45f * std::clamp(pitch / 0.6f, 0.0f, 1.0f));
        float rh = D * std::tan(std::min(tall, 1.2f));
        if (rw < 1) continue;
        bool far = hor > bankDist;
        float tower = h01(c.index * 7 + 1) < 0.3f ? 1.45f : 1.0f;  // some clouds grow into towers
        float hs = std::clamp(rh / rw * hsK * tower * (1.0f - 0.4f * strength), 0.55f, 1.6f);
        auto add = [&](float cx, float cy, float cz, float r, float shp, float seed, float a) {
            Inst it{};
            it.center[0] = cx, it.center[1] = cy, it.center[2] = cz, it.center[3] = r;
            it.shape[0] = shp, it.shape[1] = seed, it.shape[2] = std::min(1.0f, a), it.shape[3] = 1 - c.falloff;
            float blend = 1 - c.falloff, mx = 0, lit[3];
            for (int i = 0; i < 3; i++) {
                float cc = s.cloudColor[i] * s.cloudScale, ce = s.centerColor[i] * s.centerScale;
                lit[i] = ce + (cc - ce) * blend * 0.25f;
                mx = std::max(mx, lit[i]);
            }
            for (int i = 0; i < 3; i++) it.base[i] = std::min(1.0f, lit[i] / std::max(mx, 1e-3f) * std::clamp(mx, 0.0f, 1.0f));
            it.base[3] = far ? 1.0f : 0.0f;
            inst.push_back(it);
            dist.push_back(std::sqrt(cx * cx + cy * cy + cz * cz));
        };
        add(x, y, z, rw, hs, (float)c.index * 7.31f + 3.0f, c.alpha);
        if (far && bankK > 0) {
            // satellites along the horizon (perpendicular to the view direction), lower and a bit smaller
            float tx = -z / hor, tz = x / hor;
            int ns = 2 + (h01(c.index * 13 + 5) < 0.5f ? 1 : 0);
            for (int j = 0; j < ns; j++) {
                float sgn = (j & 1) ? -1.0f : 1.0f;
                float off = rw * bankK * (0.85f + 0.5f * h01(c.index * 31 + j)) * (1.0f + 0.6f * (j / 2));
                float r2 = rw * (0.7f + 0.3f * h01(c.index * 17 + j));
                float hs2 = std::clamp(hs * (0.7f + 0.6f * h01(c.index * 19 + j)), 0.55f, 1.6f);
                add(x + tx * off * sgn, y - rw * 0.08f, z + tz * off * sgn, r2, hs2, (float)c.index * 7.31f + 50.0f + j * 11.0f, c.alpha);
            }
        }
    }
    std::vector<int> order(inst.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return dist[a] > dist[b]; });
    std::vector<Inst> sorted;
    for (int i : order) sorted.push_back(inst[i]);
    g.extra[3] = (float)sorted.size();
    if (logOn && R.frame % 150 == 0) {
        LOG("[clouds] frame %llu: %d game clouds, %zu instances, count %d strength %.2f time %.1f fovy %.1f light %.2f %.2f %.2f day %.2f warm %.2f "
            "cloud %.2f %.2f %.2f x%.2f centre %.2f %.2f %.2f x%.2f fog %.2f %.2f %.2f",
            (unsigned long long)R.frame, s.n, sorted.size(), s.count, strength, s.curTime, s.fovy, L[0], L[1], L[2], day, warm,
            s.cloudColor[0], s.cloudColor[1], s.cloudColor[2], s.cloudScale, s.centerColor[0], s.centerColor[1], s.centerColor[2],
            s.centerScale, s.fogColor[0], s.fogColor[1], s.fogColor[2]);
        if (R.passColor[0])
            LOG("[clouds]   target %lux%lu", (unsigned long)R.passColor[0]->tex.width, (unsigned long)R.passColor[0]->tex.height);
    }

    [enc setDepthStencilState:depthState ? depthState : noDepth];
    [enc setCullMode:MTLCullModeNone];
    [enc setDepthBias:0 slopeScale:0 clamp:0];
    [enc setViewport:vp];
    [enc setVertexBytes:&g length:sizeof g atIndex:0];
    [enc setFragmentBytes:&g length:sizeof g atIndex:0];
    if ((what & kGrade) && haze > 0) {
        if (id<MTLRenderPipelineState> p = pipe(@"vol_vs", @"grade_fs")) {
            [enc setRenderPipelineState:p];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    }
    if (!(what & kClouds) || mode == cloud_proto::kHide || mode == cloud_proto::kMark || sorted.empty()) return;
    id<MTLBuffer> ib = [R.device newBufferWithBytes:sorted.data() length:sorted.size() * sizeof(Inst)
                                            options:MTLResourceStorageModeShared];
    [enc setVertexBuffer:ib offset:0 atIndex:1];
    [enc setFragmentBuffer:ib offset:0 atIndex:1];
    [enc setFragmentTexture:noise atIndex:0];
    [enc setFragmentSamplerState:samp atIndex:0];
    static const int repeat = getenv("WWHD_CLOUDS_REPEAT") ? std::max(1, atoi(getenv("WWHD_CLOUDS_REPEAT"))) : 1;
    if (mode == cloud_proto::kVolume || mode == cloud_proto::kCombo) {
        if (id<MTLRenderPipelineState> p = pipe(@"vol_vs", @"vol_fs")) {
            [enc setRenderPipelineState:p];
            for (int r = 0; r < repeat; r++) [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    }
    if (mode == cloud_proto::kSdf || mode == cloud_proto::kCombo) {
        std::vector<Inst> near;
        const std::vector<Inst>* list = &sorted;
        if (mode == cloud_proto::kCombo) {
            for (auto& it : sorted) if (it.base[3] < 0.5f) near.push_back(it);
            list = &near;
            if (near.empty()) return;
            ib = [R.device newBufferWithBytes:near.data() length:near.size() * sizeof(Inst) options:MTLResourceStorageModeShared];
            [enc setVertexBuffer:ib offset:0 atIndex:1];
            [enc setFragmentBuffer:ib offset:0 atIndex:1];
            Globals g2 = g;
            g2.extra[0] = envf("WWHD_CLOUDS_STEPS_C", 48.0f);
            [enc setVertexBytes:&g2 length:sizeof g2 atIndex:0];
            [enc setFragmentBytes:&g2 length:sizeof g2 atIndex:0];
        }
        if (id<MTLRenderPipelineState> p = pipe(@"cl_vs", @"cl_fs")) {
            [enc setRenderPipelineState:p];
            for (int r = 0; r < repeat; r++)
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:list->size()];
        }
    }
}

// ---- GPU timing (WWHD_CLOUDS_TIME): the cloud draws get their own command buffer
static std::atomic<uint64_t> g_time_ns{0}, g_time_n{0};
static void timed_commit(const char* what) {
    end_encoder();
    if (!R.cmd) return;
    id<MTLCommandBuffer> cb = R.cmd;
    void pool_retire(id<MTLCommandBuffer> cmd);
    pool_retire(cb);
    [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
        double t = b.GPUEndTime - b.GPUStartTime;
        if (t > 0) { g_time_ns += (uint64_t)(t * 1e9); g_time_n++; }
    }];
    [cb commit];
    R.cmd = nil;
    uint64_t n = g_time_n.load();
    static uint64_t lastLog = 0;
    if (n >= lastLog + 300) {
        lastLog = n;
        LOG("[clouds] GPU time (%s): %.3f ms per cloud pass over %llu passes", what, g_time_ns.load() / 1e6 / (double)n,
            (unsigned long long)n);
        g_time_ns = 0, g_time_n = 0;
        lastLog = 0;
    }
}
void flush();

static void timed_commit(const char* what);
void flush();

// ---- full resolution (WWHD_CLOUDS_FULLRES=1): the clouds are drawn right after the draw that composites
// the half-res sky buffer into the full-resolution target, with that draw's depth test
static Surface* g_sky = nullptr;
static uint64_t g_skyFrame = ~0ull;
static uint32_t g_pendingSeq = 0;
bool clouds_sky_surface(Surface* s) { return g_pendingSeq && s == g_sky && g_skyFrame == R.frame; }
void clouds_after_sky_composite(id<MTLRenderCommandEncoder> enc, const uint32_t* regs, const MTLViewport& vp, bool hasDepth,
                                id<MTLDepthStencilState> depthState, bool (*ensure)(Surface* const*, const uint32_t*, Surface*, uint32_t)) {
    uint32_t seq = g_pendingSeq;
    g_pendingSeq = 0;
    static const bool trace = env_on("WWHD_CLOUDS_LOG");
    if (trace && R.frame % 150 == 0)
        LOG("[clouds] sky composite: target %lux%lu depth %d DB_DEPTH_CONTROL %08X vp z %.3f..%.3f",
            R.passColor[0] ? (unsigned long)R.passColor[0]->tex.width : 0, R.passColor[0] ? (unsigned long)R.passColor[0]->tex.height : 0,
            hasDepth, regs[0xA200], vp.znear, vp.zfar);
    // our own test, not the composite's EQUAL: the impostors' interpolated depth is not bit-exact 1.0,
    // so test "in front of or at the far plane" with a depth just short of it (sky only, no write)
    static id<MTLDepthStencilState> skyOnly = [] {
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionLessEqual;
        dd.depthWriteEnabled = NO;
        return [R.device newDepthStencilStateWithDescriptor:dd];
    }();
    (void)depthState;
    static const bool timing = env_on("WWHD_CLOUDS_TIME");
    if (timing && ensure) {  // the full-resolution cloud pass in a command buffer of its own
        Surface* colors[8];
        uint32_t slices[8];
        for (int i = 0; i < 8; i++) colors[i] = R.passColor[i], slices[i] = R.passColorSlice[i];
        Surface* depth = R.passDepth;
        uint32_t depthSlice = R.passDepthSlice;
        flush();
        if (!ensure(colors, slices, depth, depthSlice)) return;
        enc = R.enc;
    }
    encode(enc, *cloud_proto::snapshot(seq), vp, kClouds, hasDepth ? skyOnly : nil);
    if (timing && ensure) {
        const int mode = cloud_proto::mode();
        timed_commit(mode == cloud_proto::kVolume ? "volume, full res" : mode == cloud_proto::kCombo ? "combo, full res" : "sdf, full res");
    }
}

// draw(), right after the pass is open, for every game draw between OP_CLOUDS_BEGIN and _END: with
// WWHD_CLOUDS_PROTO=mark and timing, the game's own cards start a command buffer of their own
static void clouds_end();
void clouds_mark_start(Surface* const* colors, const uint32_t* colorSlices, Surface* depth, uint32_t depthSlice,
                       bool (*ensure)(Surface* const*, const uint32_t*, Surface*, uint32_t)) {
    static const bool timing = env_on("WWHD_CLOUDS_TIME");
    if (cloud_proto::mode() != cloud_proto::kMark || cloud_proto::g_drawn) return;
    cloud_proto::g_drawn = true;
    if (timing) {
        cloud_proto::g_on_end = clouds_end;
        flush();
        ensure(colors, colorSlices, depth, depthSlice);
    }
}

// draw(), with the draw's state set up to the scissor: true = skip the game's draw
bool clouds_draw(id<MTLRenderCommandEncoder> enc, const MTLViewport& vp, Surface* const* colors, const uint32_t* colorSlices,
                 Surface* depth, uint32_t depthSlice, bool (*ensure)(Surface* const*, const uint32_t*, Surface*, uint32_t)) {
    const int mode = cloud_proto::mode();
    static const bool timing = env_on("WWHD_CLOUDS_TIME");
    if (mode == cloud_proto::kMark || mode == cloud_proto::kOff) return false;  // the game's own cards
    if (cloud_proto::g_drawn) return true;
    cloud_proto::g_drawn = true;
    if (timing) {
        flush();
        if (!ensure(colors, colorSlices, depth, depthSlice)) return true;
        enc = R.enc;
    }
    static const bool fullres = env_on("WWHD_CLOUDS_FULLRES");
    encode(enc, *cloud_proto::snapshot(cloud_proto::g_active_seq), vp, fullres ? 0 : kClouds);
    if (fullres) {  // the clouds go into the full-resolution picture where the sky buffer is composited
        g_sky = R.passColor[0], g_skyFrame = R.frame, g_pendingSeq = cloud_proto::g_active_seq;
    }
    if (timing) timed_commit(mode == cloud_proto::kVolume ? "volume" : mode == cloud_proto::kHide ? "hide" : "sdf");
    return true;
}
// OP_CLOUDS_BEGIN: the sky buffer's pass is open (the sky dome and its haze band are drawn); the
// horizon-haze grade goes in before the clouds. Also builds the shaders at the same moment in every
// mode, so the game's cloud set does not depend on the mode (comparable runs).
bool open_target_pass(const uint32_t* regs);  // metal_draw.mm
void sky_clouds_begin(const uint32_t* regs, uint32_t seq) {
    if (render::horizon_haze() >= 1.0f && cloud_proto::mode() == cloud_proto::kOff) return;
    if ((!R.enc || !R.passColor[0]) && !open_target_pass(regs)) return;  // no open pass: the target registers' one
    Surface* t = R.passColor[0];
    if (!t || !R.enc) return;
    MTLViewport vp{0, 0, (double)t->tex.width, (double)t->tex.height, 0, 1};
    encode(R.enc, *cloud_proto::snapshot(seq), vp, kGrade);
}
// OP_CLOUDS_END (mark + timing): closes the command buffer of the game's own cards
static void clouds_end() {
    if (cloud_proto::g_drawn) timed_commit("game cards");
}

}  // namespace gfx
