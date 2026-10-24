// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A reflection probe's mip levels (nx.render:probes, docs/adr/0025-enhanced-rendering.md): each texel
// of a side, at a level, is what a surface of that level's roughness reflects toward its direction:
// the six sides the map was drawn to from the probe, laid out as the sky's (skyDirection.slang),
// averaged over GGX's spread of normals by importance sampling, as Karis's split sum prefilters them
// ("Real Shading in Unreal Engine 4", 2013), the view taken along the reflection.

import skyDirection;

// What a pass draws: the side, the roughness of its level, and how many directions it averages.
struct Pass
{
    float side;
    float roughness;
    float sampleCount;
    float padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Pass> pass;
// The six sides the map was drawn to, toward +X, -X, +Y, -Y, +Z and -Z, in linear radiance.
[[vk::binding(1, 0)]]
Texture2DArray capture;
[[vk::binding(2, 0)]]
SamplerState linearClamp;

static const float kPi = 3.14159265;
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0; // From the side's top left.
};

// One triangle covering the target, from its three vertices' numbers.
[shader("vertex")]
VertexOutput fullscreenMain(uint vertex : SV_VertexID)
{
    const float2 uv = float2(float((vertex << 1) & 2), float(vertex & 2));
    VertexOutput output;
    output.position = float4((uv.x * 2.0) - 1.0, 1.0 - (uv.y * 2.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}

// The direction of the point `uv` of side `side`: the inverse of skyPointOf and skyUvOf.
float3 directionOf(uint side, float2 uv)
{
    const float s = (uv.x * 2.0) - 1.0;
    const float t = 1.0 - (uv.y * 2.0);
    switch (side)
    {
    case 0:
        return float3(1.0, -s, t);
    case 1:
        return float3(-1.0, s, t);
    case 2:
        return float3(s, 1.0, t);
    case 3:
        return float3(-s, -1.0, t);
    case 4:
        return float3(-t, -s, 1.0);
    default:
        return float3(t, -s, -1.0);
    }
}

float3 captured(float3 direction)
{
    const SkyPoint point = skyPointOf(direction);
    return capture.SampleLevel(linearClamp, float3(skyUvOf(point), point.side), 0.0).rgb;
}

// The `index`th of `count` points of Hammersley's sequence.
float2 hammersleyOf(uint index, uint count)
{
    return float2(float(index) / float(count), float(reversebits(index)) * 2.3283064365386963e-10);
}

// A normal of GGX's spread around `normal`, for the point `xi`.
float3 sampledNormalOf(float2 xi, float3 normal, float roughness)
{
    const float alpha = roughness * roughness;
    const float phi = 2.0 * kPi * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (((alpha * alpha) - 1.0) * xi.y)));
    const float sinTheta = sqrt(1.0 - (cosTheta * cosTheta));
    const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize((tangent * h.x) + (bitangent * h.y) + (normal * h.z));
}

[shader("fragment")]
float4 prefilterMain(VertexOutput input) : SV_Target
{
    const float3 normal = normalize(directionOf(uint(pass.side), input.uv));
    if (pass.roughness <= 0.0)
    {
        return float4(captured(normal), 1.0);
    }
    const uint count = uint(pass.sampleCount);
    float3 sum = float3(0.0);
    float weight = 0.0;
    for (uint index = 0; index < count; ++index)
    {
        const float3 h = sampledNormalOf(hammersleyOf(index, count), normal, pass.roughness);
        const float3 l = (2.0 * dot(normal, h) * h) - normal;
        const float nDotL = dot(normal, l);
        if (nDotL > 0.0)
        {
            sum += captured(l) * nDotL;
            weight += nDotL;
        }
    }
    return float4(sum / max(weight, 1e-4), 1.0);
}
