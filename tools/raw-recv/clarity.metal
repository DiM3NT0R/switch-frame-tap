// EASU and RCAS equations adapted from AMD FidelityFX FSR 1 (ffx_fsr1.h).
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
struct Params { float2 size; float strength; uint cubic; float2 output_size; uint easu; };
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

void easu_direction(thread float2 &dir, thread float &len, float w,
                    float a, float b, float c, float d, float e)
{
    float2 gradient = float2(d - b, e - a);
    float2 extent = max(abs(float2(d - c, e - c)), abs(float2(c - b, c - a)));
    float2 edge = saturate(abs(gradient) / max(extent, float2(1e-8)));
    dir += gradient * w;
    len += dot(edge, edge) * w;
}

// AMD's 12-tap EASU kernel, with source RGB reconstructed from the YUV planes.
float3 easu(texture2d<float> y, texture2d<float> u, texture2d<float> v,
            float2 uv, constant Params &p)
{
    float2 pos = uv * p.size - 0.5, base = floor(pos), pp = pos - base;
    const float2 offsets[12] = {float2(0,-1), float2(1,-1), float2(-1,0),
        float2(0,0), float2(1,0), float2(2,0), float2(-1,1), float2(0,1),
        float2(1,1), float2(2,1), float2(0,2), float2(1,2)};
    float3 c[12]; float l[12];
    for (uint i = 0; i < 12; i++) {
        float2 q = (base + offsets[i] + 0.5) / p.size;
        float yy = (y.sample(linear_sampler, q).r - 16.0/255.0) * (255.0/219.0);
        float cb = (u.sample(linear_sampler, q).r - 128.0/255.0) * (255.0/224.0);
        float cr = (v.sample(linear_sampler, q).r - 128.0/255.0) * (255.0/224.0);
        c[i] = saturate(float3(yy+1.5748*cr, yy-.187324*cb-.468124*cr, yy+1.8556*cb));
        l[i] = dot(c[i], float3(.5,1,.5));
    }
    float2 dir = 0; float len = 0;
    easu_direction(dir,len,(1-pp.x)*(1-pp.y),l[0],l[2],l[3],l[4],l[7]);
    easu_direction(dir,len,pp.x*(1-pp.y),l[1],l[3],l[4],l[5],l[8]);
    easu_direction(dir,len,(1-pp.x)*pp.y,l[3],l[6],l[7],l[8],l[10]);
    easu_direction(dir,len,pp.x*pp.y,l[4],l[7],l[8],l[9],l[11]);
    float norm = dot(dir,dir);
    dir = norm < 1.0/32768.0 ? float2(1,0) : dir * rsqrt(norm);
    len *= .5; len *= len;
    float stretch = dot(dir,dir) / max(abs(dir.x),abs(dir.y));
    float2 anisotropy = float2(1+(stretch-1)*len,1-.5*len);
    float lob = .5 + (.21-.5)*len;
    float3 sum = 0; float weight = 0;
    for (uint i = 0; i < 12; i++) {
        float2 off = offsets[i]-pp;
        float2 rotated = float2(dot(off,dir),dot(off,float2(-dir.y,dir.x))) * anisotropy;
        float distance = min(dot(rotated,rotated),1/lob);
        float b = .4*distance-1, a = lob*distance-1;
        float w = (1.5625*b*b-.5625)*a*a;
        sum += c[i]*w; weight += w;
    }
    float3 lo = min(min(c[3],c[4]),min(c[7],c[8]));
    float3 hi = max(max(c[3],c[4]),max(c[7],c[8]));
    return clamp(sum / weight,lo,hi);
}

float3 reconstructed(texture2d<float> y, texture2d<float> u, texture2d<float> v,
                     float2 uv, constant Params &p)
{
    // Clamp output neighbours before reconstruction, as an intermediate texture would.
    if (p.easu && p.cubic)
        return easu(y,u,v,clamp(uv,0.5/p.output_size,1-0.5/p.output_size),p);
    return rgb(y,u,v,uv,p);
}

fragment float4 clarity_fragment(Vertex in [[stage_in]], constant Params &p [[buffer(0)]],
        texture2d<float> y [[texture(0)]], texture2d<float> u [[texture(1)]], texture2d<float> v [[texture(2)]])
{
    float2 grid = p.easu ? p.output_size : p.size;
    float2 dx = float2(1 / grid.x, 0), dy = float2(0, 1 / grid.y);
    float3 e = reconstructed(y, u, v, in.uv, p);
    if (p.strength <= 0) return float4(e, 1);
    float3 b = reconstructed(y, u, v, in.uv - dy, p);
    float3 d = reconstructed(y, u, v, in.uv - dx, p);
    float3 f = reconstructed(y, u, v, in.uv + dx, p);
    float3 h = reconstructed(y, u, v, in.uv + dy, p);
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
