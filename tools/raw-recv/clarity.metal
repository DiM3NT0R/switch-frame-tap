// RCAS equations adapted from AMD FidelityFX FSR 1 (ffx_fsr1.h).
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#include <metal_stdlib>
using namespace metal;
struct Vertex { float4 position [[position]]; float2 uv; };
struct Params { float2 size; float strength; uint cubic; };
vertex Vertex clarity_vertex(uint id [[vertex_id]])
{
    const float2 uv[3] = {float2(0, 0), float2(2, 0), float2(0, 2)};
    return {float4(uv[id].x * 2 - 1, 1 - uv[id].y * 2, 0, 1), uv[id]};
}
constexpr sampler linear_sampler(coord::normalized, address::clamp_to_edge, filter::linear);

float luma(texture2d<float> y, float2 uv, constant Params &p)
{
    if (!p.cubic) return y.sample(linear_sampler, uv).r;
    // Catmull-Rom reconstruction: combine the two positive inner weights.
    float2 pos = uv * p.size - 0.5;
    float2 base = floor(pos), f = pos - base;
    float2 f2 = f * f, f3 = f2 * f;
    float2 w0 = -0.5 * f + f2 - 0.5 * f3;
    float2 w1 = 1 - 2.5 * f2 + 1.5 * f3;
    float2 w2 = 0.5 * f + 2 * f2 - 1.5 * f3;
    float2 w3 = -0.5 * f2 + 0.5 * f3;
    float2 w12 = w1 + w2;
    float2 c0 = (base - 0.5) / p.size;
    float2 c12 = (base + 0.5 + w2 / w12) / p.size;
    float2 c3 = (base + 2.5) / p.size;
    float result =
        y.sample(linear_sampler, float2(c0.x, c0.y)).r * w0.x * w0.y +
        y.sample(linear_sampler, float2(c12.x, c0.y)).r * w12.x * w0.y +
        y.sample(linear_sampler, float2(c3.x, c0.y)).r * w3.x * w0.y +
        y.sample(linear_sampler, float2(c0.x, c12.y)).r * w0.x * w12.y +
        y.sample(linear_sampler, c12).r * w12.x * w12.y +
        y.sample(linear_sampler, float2(c3.x, c12.y)).r * w3.x * w12.y +
        y.sample(linear_sampler, float2(c0.x, c3.y)).r * w0.x * w3.y +
        y.sample(linear_sampler, float2(c12.x, c3.y)).r * w12.x * w3.y +
        y.sample(linear_sampler, c3).r * w3.x * w3.y;
    // Clamp reconstruction overshoot to the four surrounding input texels.
    int2 i = int2(base), hi = int2(p.size) - 1;
    float a = y.read(uint2(clamp(i, int2(0), hi))).r;
    float b = y.read(uint2(clamp(i + int2(1, 0), int2(0), hi))).r;
    float c = y.read(uint2(clamp(i + int2(0, 1), int2(0), hi))).r;
    float d = y.read(uint2(clamp(i + 1, int2(0), hi))).r;
    return clamp(result, min(min(a, b), min(c, d)), max(max(a, b), max(c, d)));
}

float3 rgb(texture2d<float> y, texture2d<float> u, texture2d<float> v,
           float2 uv, constant Params &p)
{
    float yy = (luma(y, uv, p) - 16.0 / 255.0) * (255.0 / 219.0);
    float cb = (u.sample(linear_sampler, uv).r - 128.0 / 255.0) * (255.0 / 224.0);
    float cr = (v.sample(linear_sampler, uv).r - 128.0 / 255.0) * (255.0 / 224.0);
    return saturate(float3(yy + 1.5748 * cr, yy - 0.187324 * cb - 0.468124 * cr, yy + 1.8556 * cb));
}

fragment float4 clarity_fragment(Vertex in [[stage_in]], constant Params &p [[buffer(0)]],
        texture2d<float> y [[texture(0)]], texture2d<float> u [[texture(1)]], texture2d<float> v [[texture(2)]])
{
    float2 dx = float2(1 / p.size.x, 0), dy = float2(0, 1 / p.size.y);
    float3 e = rgb(y, u, v, in.uv, p);
    if (p.strength <= 0) return float4(e, 1);
    float3 b = rgb(y, u, v, in.uv - dy, p);
    float3 d = rgb(y, u, v, in.uv - dx, p);
    float3 f = rgb(y, u, v, in.uv + dx, p);
    float3 h = rgb(y, u, v, in.uv + dy, p);
    float3 mn = min(min(b, d), min(f, h)), mx = max(max(b, d), max(f, h));
    float3 hit_min = min(mn, e) / max(4 * mx, float3(1e-5));
    float3 hit_max = (1 - max(mx, e)) / min(4 * mn - 4, float3(-1e-5));
    float3 lobes = max(-hit_min, hit_max);
    float lobe = max(-0.1875, min(max(lobes.r, max(lobes.g, lobes.b)), 0.0)) * p.strength;
    float3 weights = float3(0.5, 1, 0.5);
    float bl = dot(b, weights), dl = dot(d, weights), el = dot(e, weights);
    float fl = dot(f, weights), hl = dot(h, weights);
    float range = max(max(max(bl, dl), el), max(fl, hl)) - min(min(min(bl, dl), el), min(fl, hl));
    float noise = saturate(abs(0.25 * (bl + dl + fl + hl) - el) / max(range, 1e-5));
    lobe *= 1 - 0.5 * noise;
    return float4(saturate((lobe * (b + d + f + h) + e) / (4 * lobe + 1)), 1);
}
