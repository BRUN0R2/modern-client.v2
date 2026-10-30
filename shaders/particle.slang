// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Particles (nx.render:particle): each a quad the CPU turns to face the view, the engine's dot times
// the particle's color, as opaque as the dot and the particle make it, as the original's GL draws it
// (GL_MODULATE). Drawn as linear light (colorEncoding.slang), the particle's color, a display color, is
// made light; how opaque, as it is. The particles are seen through the frame's fog (fog.slang), the
// streaks, added, through none, as the original's fog leaves what is added.

import colorEncoding;
import fog;

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // The fog the particles are seen through.
    Fog fog;
    // How the colors reach the target: kDisplayEncoding or kLinearEncoding.
    uint colorEncoding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;

[[vk::binding(0, 1)]]
Texture2D dotImage;
[[vk::binding(1, 1)]]
SamplerState dotSampler;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    // rgb the particle's color; a how opaque it is.
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    // In the world, where the fog is measured from.
    float3 worldPosition : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    const float4 point = float4(input.position, 1.0);
    VertexOutput output;
    output.position = float4(dot(frame.rows[0], point), dot(frame.rows[1], point), dot(frame.rows[2], point),
                             dot(frame.rows[3], point));
    output.uv = input.uv;
    output.color = input.color;
    output.worldPosition = input.position;
    return output;
}

// The dot times the color.
float4 colorOf(VertexOutput input)
{
    const float4 texel = dotImage.Sample(dotSampler, input.uv);
    return float4(texel.rgb * encodedOf(input.color.rgb, frame.colorEncoding), texel.a * input.color.a);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = colorOf(input);
    return float4(foggedOf(color.rgb, frame.fog, input.worldPosition), color.a);
}

// A streak's, added through no fog.
[shader("fragment")]
float4 addedFragmentMain(VertexOutput input) : SV_Target
{
    return colorOf(input);
}
