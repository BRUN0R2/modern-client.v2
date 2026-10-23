// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The enhanced look's last passes (nx.render:post, docs/adr/0025-enhanced-rendering.md): the HDR scene
// reduced into a chain of smaller images and enlarged back, as Call of Duty: Advanced Warfare's bloom
// does (Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH
// 2014): thirteen reads a texel of the smaller image, its first level averaged by brightness so that a
// single bright texel does not flicker, and a 3x3 tent as it is enlarged; then the scene, with a share
// of its bloom, mapped to the screen by a shoulder: below the knee the light as it is, above it rolled
// off towards white, its hue kept.

// What a pass reads: one over the size of its source, and for the tone pass the bloom's share and the
// knee.
struct Pass
{
    float2 sourceTexelSize;
    float bloomStrength;
    float knee;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Pass> pass;
[[vk::binding(1, 0)]]
Texture2D source;
[[vk::binding(2, 0)]]
SamplerState linearClamp;
// The tone pass's bloom, the chain enlarged back to its first level.
[[vk::binding(3, 0)]]
Texture2D bloom;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0; // From the target's top left.
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

float3 read(float2 uv, float2 offset)
{
    return source.SampleLevel(linearClamp, uv + (offset * pass.sourceTexelSize), 0.0).rgb;
}

// How bright a color is to the eye.
float luminanceOf(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// A box's weight, Karis's average: the brighter, the less, so that one bright texel cannot flash.
float karisWeightOf(float3 box)
{
    return 1.0 / (1.0 + luminanceOf(box));
}

// The thirteen reads: four boxes of four around the middle and the box at the middle, which counts
// half; with `isFirst`, each box weighted by Karis's average.
float3 reduce(float2 uv, bool isFirst)
{
    const float3 a = read(uv, float2(-2.0, -2.0));
    const float3 b = read(uv, float2(0.0, -2.0));
    const float3 c = read(uv, float2(2.0, -2.0));
    const float3 d = read(uv, float2(-2.0, 0.0));
    const float3 e = read(uv, float2(0.0, 0.0));
    const float3 f = read(uv, float2(2.0, 0.0));
    const float3 g = read(uv, float2(-2.0, 2.0));
    const float3 h = read(uv, float2(0.0, 2.0));
    const float3 i = read(uv, float2(2.0, 2.0));
    const float3 j = read(uv, float2(-1.0, -1.0));
    const float3 k = read(uv, float2(1.0, -1.0));
    const float3 l = read(uv, float2(-1.0, 1.0));
    const float3 m = read(uv, float2(1.0, 1.0));
    const float3 middle = (j + k + l + m) * 0.25;
    const float3 topLeft = (a + b + d + e) * 0.25;
    const float3 topRight = (b + c + e + f) * 0.25;
    const float3 bottomLeft = (d + e + g + h) * 0.25;
    const float3 bottomRight = (e + f + h + i) * 0.25;
    if (!isFirst)
    {
        return (middle * 0.5) + ((topLeft + topRight + bottomLeft + bottomRight) * 0.125);
    }
    const float weightMiddle = karisWeightOf(middle) * 0.5;
    const float weightTopLeft = karisWeightOf(topLeft) * 0.125;
    const float weightTopRight = karisWeightOf(topRight) * 0.125;
    const float weightBottomLeft = karisWeightOf(bottomLeft) * 0.125;
    const float weightBottomRight = karisWeightOf(bottomRight) * 0.125;
    const float total = weightMiddle + weightTopLeft + weightTopRight + weightBottomLeft + weightBottomRight;
    return ((middle * weightMiddle) + (topLeft * weightTopLeft) + (topRight * weightTopRight) +
            (bottomLeft * weightBottomLeft) + (bottomRight * weightBottomRight)) /
           total;
}

// The bloom's first level, from the scene.
[shader("fragment")]
float4 reduceFirstMain(VertexOutput input) : SV_Target
{
    return float4(reduce(input.uv, true), 1.0);
}

// Each level after, from the one before.
[shader("fragment")]
float4 reduceMain(VertexOutput input) : SV_Target
{
    return float4(reduce(input.uv, false), 1.0);
}

// A level enlarged into the one before it, added to what that level holds: a 3x3 tent of reads a
// texel of the source apart.
[shader("fragment")]
float4 enlargeMain(VertexOutput input) : SV_Target
{
    const float3 tent = (read(input.uv, float2(-1.0, -1.0)) + (read(input.uv, float2(0.0, -1.0)) * 2.0) +
                         read(input.uv, float2(1.0, -1.0)) + (read(input.uv, float2(-1.0, 0.0)) * 2.0) +
                         (read(input.uv, float2(0.0, 0.0)) * 4.0) + (read(input.uv, float2(1.0, 0.0)) * 2.0) +
                         read(input.uv, float2(-1.0, 1.0)) + (read(input.uv, float2(0.0, 1.0)) * 2.0) +
                         read(input.uv, float2(1.0, 1.0))) /
                        16.0;
    return float4(tent, 1.0);
}

// Below `knee` the value as it is; above it, rolled off towards 1, which it never passes.
float shoulderOf(float value, float knee)
{
    if (value <= knee)
    {
        return value;
    }
    const float room = 1.0 - knee;
    return knee + (room * (1.0 - exp(-(value - knee) / room)));
}

// The scene with its share of bloom, its brightest channel through the shoulder and the others with
// it, so that a bright color whitens no more than its hue allows.
[shader("fragment")]
float4 toneMain(VertexOutput input) : SV_Target
{
    const float3 scene = source.SampleLevel(linearClamp, input.uv, 0.0).rgb;
    const float3 blurred = bloom.SampleLevel(linearClamp, input.uv, 0.0).rgb;
    const float3 color = lerp(scene, blurred, pass.bloomStrength);
    const float peak = max(color.r, max(color.g, color.b));
    const float3 mapped = peak > 0.0 ? color * (shoulderOf(peak, pass.knee) / peak) : color;
    return float4(mapped, 1.0);
}
