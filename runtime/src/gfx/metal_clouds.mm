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
#include "metal.h"
#include "runtime.h"

namespace gfx {

static const char* kCloudShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Globals {
    float3x3 view;      // world (camera-relative) -> camera
    float4 proj;        // f/aspect, f, aspect, time
    float4 sun;         // xyz: direction to the sun (world), w: sun weight (0 night .. 1 day)
    float4 lit;         // palette colours (shader-side shades are derived from these)
    float4 sky;
    float4 params;      // x: band softness, y: noise strength, z: grade, w: mode
    float4 tune;        // x: fringe width, y: palette saturation, z: shadow level, w: mid level
    float4 extra;       // x: max march steps, y: 1 = cheap normals (blob field only)
};
struct Inst {
    float4 center;      // xyz: cloud base centre (camera-relative world), w: width radius
    float4 shape;       // x: height/width, y: seed, z: alpha, w: colour blend (1 - falloff)
    float4 base;        // rgb: the game's colour for this cloud
};
struct VOut {
    float4 pos [[position]];
    float3 world;
    uint inst [[flat]];
};

vertex VOut cl_vs(uint vid [[vertex_id]], uint iid [[instance_id]], constant Globals& g [[buffer(0)]],
                  const device Inst* inst [[buffer(1)]]) {
    Inst c = inst[iid];
    float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
    float r = c.center.w * max(1.0, c.shape.x) * 1.5;
    float3 mid = c.center.xyz + float3(0, c.center.w * c.shape.x * 0.55, 0);
    float3 right = float3(g.view[0][0], g.view[1][0], g.view[2][0]);
    float3 up = float3(g.view[0][1], g.view[1][1], g.view[2][1]);
    float3 p = mid + (corner.x * right + corner.y * up) * r;
    float3 q = g.view * p;
    VOut o;
    float w = -q.z;
    o.pos = float4(q.x * g.proj.x, q.y * g.proj.y, 0.5 * w, w);
    o.world = p;
    o.inst = iid;
    return o;
}

// ---- shape: a few spheres per cloud, smooth union, flat base, baked noise displacement
static float hash1(float n) { return fract(sin(n) * 43758.5453); }
static float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}
static float blobs(float3 p, float hs, float seed) {
    float d = 1e9;
    // base row
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float x = -0.85 + fi * 0.57 + (hash1(seed + fi) - 0.5) * 0.25;
        float r = 0.42 + hash1(seed + fi + 7.0) * 0.16;
        float z = (hash1(seed + fi + 13.0) - 0.5) * 0.35;
        d = smin(d, length(p - float3(x, r * 0.75 * hs, z)) - r, 0.12);
    }
    // middle
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float x = -0.5 + fi * 0.5 + (hash1(seed + fi + 21.0) - 0.5) * 0.3;
        float r = 0.36 + hash1(seed + fi + 29.0) * 0.18;
        float y = (0.55 + hash1(seed + fi + 37.0) * 0.25) * hs;
        d = smin(d, length(p - float3(x, y, (hash1(seed + fi + 41.0) - 0.5) * 0.3)) - r, 0.1);
    }
    // crown
    for (int i = 0; i < 2; i++) {
        float fi = float(i);
        float cx = (hash1(seed + 53.0 + fi) - 0.5) * 0.7;
        d = smin(d, length(p - float3(cx, (0.95 + hash1(seed + 59.0 + fi) * 0.25) * hs, 0.0)) -
                    (0.26 + hash1(seed + 61.0 + fi) * 0.12), 0.1);
    }
    // flat base
    d = max(d, -(p.y - 0.06));
    return d;
}
static float sdf(float3 p, float hs, float seed, texture3d<float> noise, sampler s, float amp, float ero) {
    float d = blobs(p, hs, seed) + ero;
    if (d > 0.35) return d;  // far: skip the noise lookups
    float3 np = p * 0.8 + float3(seed * 0.37, 0.0, seed * 0.11);
    // billows: inverted Worley at the base frequency, value-noise fBm for detail (Perlin-Worley style)
    float n = noise.sample(s, np).g * 0.6 + noise.sample(s, np * 2.7).r * 0.4;
    return d + (0.5 - n) * amp;
}

struct FOut { float4 c [[color(0)]]; };

fragment FOut cl_fs(VOut in [[stage_in]], constant Globals& g [[buffer(0)]], const device Inst* inst [[buffer(1)]],
                    texture3d<float> noise [[texture(0)]], sampler s [[sampler(0)]]) {
    Inst c = inst[in.inst];
    float hs = c.shape.x, seed = c.shape.y, amp = g.params.y;
    // the game's fade (alpha) erodes the shape instead of making it translucent: clouds grow in and
    // dissolve with toon edges (Sea of Thieves' alpha threshold idea)
    float ero = (1.0 - saturate(c.shape.z * 1.25)) * 0.55;
    float3 rd = normalize(in.world);
    float3 ro = -c.center.xyz / c.center.w;  // eye in cloud units
    // bounding sphere
    float3 bc = float3(0, 0.55 * hs, 0);
    float br = 1.45 * max(1.0, hs);
    float3 oc = ro - bc;
    float b = dot(oc, rd), cc = dot(oc, oc) - br * br, h = b * b - cc;
    if (h < 0.0) discard_fragment();
    h = sqrt(h);
    float t = max(-b - h, 0.0), t1 = -b + h;
    float dmin = 1e9, tmin = t;
    bool hit = false;
    float3 p;
    float eps = 0.0025 * t;
    const int maxSteps = int(g.extra.x);
    for (int i = 0; i < maxSteps && t < t1; i++) {
        p = ro + rd * t;
        float d = sdf(p, hs, seed, noise, s, amp, ero);
        if (d < dmin) { dmin = d; tmin = t; }
        if (d < eps) { hit = true; break; }
        t += max(d * 0.85, 0.01);
    }
    float3 sunDir = g.sun.xyz;
    float3 base = c.base.rgb;
    // shades from the palette: lit (slightly above the game's colour), mid, cool shadow
    float luma = dot(base, float3(0.3, 0.59, 0.11));
    float3 litC = mix(float3(luma), base, g.tune.y);
    float3 shadowC = mix(litC * g.tune.z, g.sky.rgb, 0.3);
    float3 midC = mix(litC * g.tune.w, g.sky.rgb, 0.08);
    // aerial perspective: distant clouds (near the horizon) lean toward a hazy sky colour
    float3 haze = mix(g.sky.rgb, float3(1.0), 0.55);
    float far = c.base.w * 0.3;
    litC = mix(litC, haze, far), midC = mix(midC, haze, far), shadowC = mix(shadowC, haze, far);
    FOut o;
    if (!hit) {
        // frayed fringe: a thin, noise-broken halo just outside the surface, sun-side brighter
        float fw = g.tune.x;
        if (dmin > fw) discard_fragment();
        float3 q = ro + rd * tmin;
        float n = noise.sample(s, q * 1.9 + seed).g;
        float a = (1.0 - dmin / fw) * smoothstep(0.35, 0.75, n);
        if (a < 0.02) discard_fragment();
        float3 col = mix(midC, litC, saturate(q.y / (1.2 * hs)));
        a *= saturate(c.shape.z * 4.0) * 0.85;
        o.c = float4(col * a, a);
        return o;
    }
    // normals: the toon bands use the smooth blob field's gradient (proxy normal, Guilty Gear style:
    // large clean light shapes), the noise-displaced one only adds a little detail and the rim
    const float e = 0.01;
    float3 nb = normalize(float3( 1, -1, -1) * blobs(p + float3( 1, -1, -1) * 0.06, hs, seed) +
                          float3(-1, -1,  1) * blobs(p + float3(-1, -1,  1) * 0.06, hs, seed) +
                          float3(-1,  1, -1) * blobs(p + float3(-1,  1, -1) * 0.06, hs, seed) +
                          float3( 1,  1,  1) * blobs(p + float3( 1,  1,  1) * 0.06, hs, seed));
    float3 nd = nb;
    if (g.extra.y < 0.5)
        nd = normalize(float3( 1, -1, -1) * sdf(p + float3( 1, -1, -1) * e, hs, seed, noise, s, amp, ero) +
                       float3(-1, -1,  1) * sdf(p + float3(-1, -1,  1) * e, hs, seed, noise, s, amp, ero) +
                       float3(-1,  1, -1) * sdf(p + float3(-1,  1, -1) * e, hs, seed, noise, s, amp, ero) +
                       float3( 1,  1,  1) * sdf(p + float3( 1,  1,  1) * e, hs, seed, noise, s, amp, ero));
    float3 n = normalize(mix(nb, nd, 0.12));
    float ndl = dot(n, sunDir) * 0.5 + 0.5;
    float height = saturate(p.y / (1.1 * hs));
    float lightv = ndl * 0.55 + height * 0.45;
    // optional self-shadowing (idea from Nubis / Frostnova's light-occlusion grid, done here as a short
    // soft-shadow march toward the sun through the smooth blob field): puffs shadow each other
    if (g.params.w > 0.0) {
        float sh = 1.0, ts = 0.06;
        for (int i = 0; i < int(g.params.w); i++) {
            float dd = blobs(p + sunDir * ts, hs, seed);
            sh = min(sh, saturate(6.0 * dd / ts + 0.5));
            ts += max(dd, 0.08);
        }
        lightv *= mix(0.45, 1.0, sh);
    }
    float sw = g.params.x;
    float3 col = mix(shadowC, midC, smoothstep(0.30 - sw, 0.30 + sw, lightv));
    col = mix(col, litC, smoothstep(0.43 - sw, 0.43 + sw, lightv));
    // rim / silver lining on the sun side of the silhouette
    float facing = saturate(dot(n, -rd));
    // rim / silver lining: silhouette (detail normal) x sun side, stronger looking toward the sun (forward scatter)
    // forward scatter: looking toward the sun (dusk, back-lit clouds) the silver lining gets much stronger
    float fwd = 0.6 + 1.4 * pow(saturate(dot(rd, sunDir)), 3.0);
    float rim = pow(1.0 - facing, 3.0) * saturate(dot(nb, sunDir) + 0.35) * fwd * g.sun.w;
    col = mix(col, min(litC + 0.10, float3(1.0)), smoothstep(0.30 - sw, 0.30 + sw, rim));
    // underside: cooler and darker where the surface faces down
    col = mix(col, shadowC * 0.92, smoothstep(-0.35, -0.75, n.y) * 0.6);
    float a = saturate(c.shape.z * 6.0);
    o.c = float4(col * a, a);
    return o;
}

// ---- option D: one full-screen raymarched slab with posterised light
struct FVOut { float4 pos [[position]]; float2 ndc; };
vertex FVOut vol_vs(uint vid [[vertex_id]]) {
    float2 p = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
    FVOut o; o.pos = float4(p, 0.5, 1); o.ndc = p; return o;
}
fragment FOut vol_fs(FVOut in [[stage_in]], constant Globals& g [[buffer(0)]], const device Inst* inst [[buffer(1)]],
                     constant int& count [[buffer(2)]], texture3d<float> noise [[texture(0)]], sampler s [[sampler(0)]]) {
    float3 dc = normalize(float3(in.ndc.x / g.proj.x, in.ndc.y / g.proj.y, -1.0));
    float3 rd = transpose(g.view) * dc;
    FOut o;
    if (rd.y < 0.01) discard_fragment();
    const float lo = 1800.0, hi = 4200.0;
    float t0 = lo / rd.y, t1 = hi / rd.y;
    t1 = min(t1, t0 + 9000.0);
    const int N = 40;
    float dt = (t1 - t0) / float(N);
    float T = 1.0;
    float3 acc = 0.0;
    float3 base = g.lit.rgb;
    float3 litC = min(base * 1.12 + 0.04, float3(1.0));
    float3 shadowC = mix(base * float3(0.66, 0.72, 0.86), g.sky.rgb, 0.22);
    for (int i = 0; i < N && T > 0.03; i++) {
        float t = t0 + dt * (float(i) + 0.5);
        float3 p = rd * t;
        // coverage: the game's clouds as soft discs
        float cov = 0.0;
        for (int k = 0; k < count; k++) {
            float3 cpos = inst[k].center.xyz;
            float r = inst[k].center.w * 1.6;
            float2 dxz = p.xz - cpos.xz;
            cov = max(cov, inst[k].shape.z * saturate(1.0 - dot(dxz, dxz) / (r * r)));
        }
        if (cov <= 0.0) continue;
        float hn = (p.y - lo) / (hi - lo);
        float n = noise.sample(s, p * 0.00025).r * 0.6 + noise.sample(s, p * 0.0009).r * 0.4;
        float dens = saturate(cov * 1.4 - n * 0.9 - hn * hn * 0.8) * 0.004;
        if (dens <= 0.0) continue;
        // one light sample toward the sun
        float3 ps = p + g.sun.xyz * 400.0;
        float nl = noise.sample(s, ps * 0.00025).r;
        float occl = saturate(cov * 1.4 - nl * 0.9);
        float lightv = (1.0 - occl) * 0.6 + hn * 0.4;
        float band = lightv < 0.35 ? 0.0 : lightv < 0.6 ? 0.5 : 1.0;  // posterised
        float3 col = mix(shadowC, litC, band);
        float a = 1.0 - exp(-dens * dt);
        acc += T * a * col;
        T *= 1.0 - a;
    }
    float alpha = 1.0 - T;
    if (alpha < 0.01) discard_fragment();
    o.c = float4(acc, alpha);
    return o;
}

// ---- research: deepen the sky colour behind the clouds (full screen, before the clouds)
fragment FOut grade_fs(FVOut in [[stage_in]], constant Globals& g [[buffer(0)]]) {
    float3 dc = normalize(float3(in.ndc.x / g.proj.x, in.ndc.y / g.proj.y, -1.0));
    float3 rd = transpose(g.view) * dc;
    float up = saturate(rd.y * 3.0);
    FOut o;
    float a = g.params.z * up * 0.35;
    o.c = float4(float3(0.05, 0.25, 0.75) * a, a);
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
    float proj[4], sun[4], lit[4], sky[4], params[4], tune[4], extra[4];
};
struct Inst {
    float center[4], shape[4], base[4];
};

static float envf(const char* n, float d) { const char* e = getenv(n); return e && *e ? (float)atof(e) : d; }
static bool env_on(const char* n) { const char* e = getenv(n); return e && *e && strcmp(e, "0"); }

// draws the clouds of snapshot `seq` into the open encoder's pass; vp: the game's viewport
static void encode(id<MTLRenderCommandEncoder> enc, const cloud_proto::Snapshot& s, const MTLViewport& vp) {
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
    g.proj[0] = f / aspect, g.proj[1] = f, g.proj[2] = aspect, g.proj[3] = 0;
    float L[3] = {0, 1, 0};
    if (s.sunVisible) {
        float d[3] = {s.sunPos[0] - s.eye[0], s.sunPos[1] - s.eye[1], s.sunPos[2] - s.eye[2]};
        float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 1e-3f) for (int i = 0; i < 3; i++) L[i] = d[i] / len;
    }
    float sunW = std::clamp(L[1] * 4.0f + 0.2f, 0.0f, 1.0f);
    if (L[1] < 0.05f) {  // night or sun below the horizon: light from above
        L[0] *= 0.3f, L[2] *= 0.3f, L[1] = 0.9f;
        float len = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
        for (float& v : L) v /= len;
    }
    for (int i = 0; i < 3; i++) g.sun[i] = L[i];
    g.sun[3] = sunW;
    for (int i = 0; i < 3; i++) {
        g.lit[i] = std::min(1.0f, s.cloudColor[i] * s.cloudScale);
        g.sky[i] = s.fogColor[i];
    }
    static const float grade = getenv("WWHD_SKY_GRADE") ? (float)atof(getenv("WWHD_SKY_GRADE")) : 0.0f;
    g.params[0] = envf("WWHD_CLOUDS_BAND", 0.012f), g.params[1] = envf("WWHD_CLOUDS_NOISE", 0.38f), g.params[2] = grade;
    g.params[3] = envf("WWHD_CLOUDS_SELFSHADOW", 0.0f);  // shadow march steps (0: off)
    g.tune[0] = envf("WWHD_CLOUDS_FRINGE", 0.02f), g.tune[1] = envf("WWHD_CLOUDS_SAT", 0.8f);
    g.extra[0] = envf("WWHD_CLOUDS_STEPS", 40.0f), g.extra[1] = envf("WWHD_CLOUDS_CHEAP", 0.0f);
    g.tune[2] = envf("WWHD_CLOUDS_SHADOW", 0.72f), g.tune[3] = envf("WWHD_CLOUDS_MID", 0.91f);

    // instances: one per game cloud (layer 0 sizing of drawVrkumo), far to near
    std::vector<Inst> inst;
    std::vector<float> dist;
    const float* k = s.k;  // sizeBase, sizeGain, variation, pitchScale, sideGain, pitchGain, starSizeBase, farPlane
    for (int n = 0; n < s.n; n++) {
        const cloud_proto::Cloud& c = s.c[n];
        float x = c.pos[0], y = c.pos[1], z = c.pos[2];
        float hor = std::sqrt(x * x + z * z), D = std::sqrt(hor * hor + y * y);
        if (D < 1) continue;
        float v = (float)(c.index & 15) * k[2];
        float shape = 1 - v * v * v;
        float weather = k[0] * s.strength + k[1];
        float ang = shape * weather * c.falloff;
        float pitch = std::atan2(y, hor);
        float q = std::min(pitch / k[3], 1.0f), qc = q * q * q;
        float side = ang * k[0] * (1 + qc * k[4]);
        float tall = ang * (1 + c.height) * k[6] * (1 + qc * k[5]);
        float rw = D * std::tan(std::min(side, 1.2f)) * envf("WWHD_CLOUDS_SIZE", 1.0f);
        float rh = D * std::tan(std::min(tall, 1.2f));
        if (rw < 1) continue;
        Inst it{};
        it.center[0] = x, it.center[1] = y, it.center[2] = z, it.center[3] = rw;
        it.shape[0] = std::clamp(rh / rw * envf("WWHD_CLOUDS_HS", 0.6f), 0.6f, 1.25f);
        it.shape[1] = (float)c.index * 7.31f + 3.0f;
        it.shape[2] = std::min(1.0f, c.alpha);
        // lit colour: the palette's cloud centre colour (warm white at noon, golden at dusk), kept
        // bright (normalised to its largest channel, at least 0.75); the cloud colour proper is greenish
        // in the noon palette and is only blended in a little toward the horizon
        float blend = 1 - c.falloff, mx = 0;
        float lit[3];
        for (int i = 0; i < 3; i++) {
            float cc = s.cloudColor[i] * s.cloudScale, ce = s.centerColor[i] * s.centerScale;
            lit[i] = ce + (cc - ce) * blend * 0.25f;
            mx = std::max(mx, lit[i]);
        }
        float bright = std::clamp(mx, 0.75f, 1.0f) / std::max(mx, 1e-3f);
        for (int i = 0; i < 3; i++) it.base[i] = std::min(1.0f, lit[i] * bright);
        it.base[3] = blend;
        inst.push_back(it);
        dist.push_back(D);
    }
    std::vector<int> order(inst.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return dist[a] > dist[b]; });
    std::vector<Inst> sorted;
    for (int i : order) sorted.push_back(inst[i]);
    if (logOn && R.frame % 150 == 0) {
        LOG("[clouds] frame %llu: %d game clouds, %zu drawn, count %d strength %.2f time %.1f fovy %.1f eye %.0f %.0f %.0f sun %.2f %.2f %.2f (vis %d a %.2f) "
            "cloud %.2f %.2f %.2f x%.2f centre %.2f %.2f %.2f x%.2f fog %.2f %.2f %.2f k %g %g %g %g %g %g %g %g",
            (unsigned long long)R.frame, s.n, sorted.size(), s.count, s.strength, s.curTime, s.fovy, s.eye[0], s.eye[1], s.eye[2],
            L[0], L[1], L[2], s.sunVisible, s.sunAlpha, s.cloudColor[0], s.cloudColor[1], s.cloudColor[2], s.cloudScale,
            s.centerColor[0], s.centerColor[1], s.centerColor[2], s.centerScale, s.fogColor[0], s.fogColor[1], s.fogColor[2],
            k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7]);
        if (R.passColor[0])
            LOG("[clouds]   target %lux%lu", (unsigned long)R.passColor[0]->tex.width, (unsigned long)R.passColor[0]->tex.height);
        for (size_t i = 0; i < std::min<size_t>(sorted.size(), 4); i++)
            LOG("[clouds]   c %.0f %.0f %.0f r %.0f hs %.2f a %.2f", sorted[i].center[0], sorted[i].center[1], sorted[i].center[2],
                sorted[i].center[3], sorted[i].shape[0], sorted[i].shape[2]);
    }

    [enc setDepthStencilState:noDepth];
    [enc setCullMode:MTLCullModeNone];
    [enc setDepthBias:0 slopeScale:0 clamp:0];
    [enc setViewport:vp];
    [enc setVertexBytes:&g length:sizeof g atIndex:0];
    [enc setFragmentBytes:&g length:sizeof g atIndex:0];
    if (grade > 0) {
        if (id<MTLRenderPipelineState> p = pipe(@"vol_vs", @"grade_fs")) {
            [enc setRenderPipelineState:p];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    }
    if (mode == cloud_proto::kHide || sorted.empty()) return;
    // instances go in a small buffer (setBytes is limited to 4 KiB)
    id<MTLBuffer> ib = [R.device newBufferWithBytes:sorted.data() length:sorted.size() * sizeof(Inst)
                                            options:MTLResourceStorageModeShared];
    [enc setVertexBuffer:ib offset:0 atIndex:1];
    [enc setFragmentBuffer:ib offset:0 atIndex:1];
    [enc setFragmentTexture:noise atIndex:0];
    [enc setFragmentSamplerState:samp atIndex:0];
    static const int repeat = getenv("WWHD_CLOUDS_REPEAT") ? std::max(1, atoi(getenv("WWHD_CLOUDS_REPEAT"))) : 1;
    if (mode == cloud_proto::kVolume) {
        id<MTLRenderPipelineState> p = pipe(@"vol_vs", @"vol_fs");
        if (!p) return;
        [enc setRenderPipelineState:p];
        int count = (int)sorted.size();
        [enc setFragmentBytes:&count length:sizeof count atIndex:2];
        for (int r = 0; r < repeat; r++) [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    } else {
        id<MTLRenderPipelineState> p = pipe(@"cl_vs", @"cl_fs");
        if (!p) return;
        [enc setRenderPipelineState:p];
        for (int r = 0; r < repeat; r++)
            [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:sorted.size()];
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

// draw(), right after the pass is open, for every game draw between OP_CLOUDS_BEGIN and _END: with
// WWHD_CLOUDS_PROTO=mark and timing, the game's own cards start a command buffer of their own
static void clouds_end();
void clouds_mark_start(Surface* const* colors, const uint32_t* colorSlices, Surface* depth, uint32_t depthSlice,
                       bool (*ensure)(Surface* const*, const uint32_t*, Surface*, uint32_t)) {
    static const bool timing = env_on("WWHD_CLOUDS_TIME");
    if (cloud_proto::mode() != cloud_proto::kMark || !timing || cloud_proto::g_drawn) return;
    cloud_proto::g_drawn = true;
    cloud_proto::g_on_end = clouds_end;
    flush();
    ensure(colors, colorSlices, depth, depthSlice);
}
// draw(), with the draw's state set up to the scissor: true = skip the game's draw
bool clouds_draw(id<MTLRenderCommandEncoder> enc, const MTLViewport& vp, Surface* const* colors, const uint32_t* colorSlices,
                 Surface* depth, uint32_t depthSlice, bool (*ensure)(Surface* const*, const uint32_t*, Surface*, uint32_t)) {
    const int mode = cloud_proto::mode();
    static const bool timing = env_on("WWHD_CLOUDS_TIME");
    if (mode == cloud_proto::kMark) return false;
    if (cloud_proto::g_drawn) return true;
    cloud_proto::g_drawn = true;
    if (timing) {
        flush();
        if (!ensure(colors, colorSlices, depth, depthSlice)) return true;
        enc = R.enc;
    }
    encode(enc, *cloud_proto::snapshot(cloud_proto::g_active_seq), vp);
    if (timing) timed_commit(mode == cloud_proto::kVolume ? "volume" : mode == cloud_proto::kHide ? "hide" : "sdf");
    return true;
}
// OP_CLOUDS_END (mark + timing): closes the command buffer of the game's own cards
static void clouds_end() {
    if (cloud_proto::g_drawn) timed_commit("game cards");
}

}  // namespace gfx
