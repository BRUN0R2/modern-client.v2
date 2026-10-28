// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Particles (nx.render:particle): each a quad the CPU turns to face the view, the engine's dot times
// the particle's color, as opaque as the dot and the particle make it, as the original's GL draws it
// (GL_MODULATE). Drawn as linear light (colorEncoding.slang), the particle's color, a display color, is
// made light; how opaque, as it is.

import colorEncoding;

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
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
    return output;
}

// The dot times the color.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 texel = dotImage.Sample(dotSampler, input.uv);
    return float4(texel.rgb * encodedOf(input.color.rgb, frame.colorEncoding), texel.a * input.color.a);
}
