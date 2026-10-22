// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The screen (nx.render:screen): rectangles in pixels of the target from its top left, a picture
// times a color, as the original's GL draws its HUD (GL_MODULATE), a transparent texel discarded; or
// a color alone.

struct Frame
{
    // The target's size in pixels; the rest pads the buffer to 16 bytes.
    float2 size;
    float2 padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;

[[vk::binding(0, 1)]]
Texture2D image;
[[vk::binding(1, 1)]]
SamplerState imageSampler;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION; // In pixels, from the target's top left.
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

// Pixels to clip space, whose y goes up.
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4((input.position.x / frame.size.x) * 2.0 - 1.0,
                             1.0 - (input.position.y / frame.size.y) * 2.0, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

// The picture times the color; a texel with no alpha is not drawn.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 texel = image.Sample(imageSampler, input.uv);
    if (texel.a <= 0.0)
    {
        discard;
    }
    return texel * input.color;
}

// The color alone.
[shader("fragment")]
float4 colorFragmentMain(VertexOutput input) : SV_Target
{
    return input.color;
}
