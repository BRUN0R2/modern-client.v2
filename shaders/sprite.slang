// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Sprites (nx.render:sprite): each a quad the CPU turns as its sprite's orientation says, its image
// times its entity's color, as opaque as its render mode makes it, as the original's GL draws it
// (GL_MODULATE); a fragment whose alpha is not above the alpha test is discarded (GL_ALPHA_TEST). An
// alpha-tested sprite the world lights is drawn again where it was, its light multiplying what is
// there.

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;

[[vk::binding(0, 1)]]
Texture2D image;
[[vk::binding(1, 1)]]
SamplerState imageSampler;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    // rgb the entity's color, or the light; a how opaque the sprite is drawn.
    [[vk::location(2)]] float4 color : COLOR0;
    // What a fragment's alpha must be above.
    [[vk::location(3)]] float alphaTest : TEXCOORD1;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation float alphaTest : TEXCOORD1;
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
    output.alphaTest = input.alphaTest;
    return output;
}

// The image times the color, cut by the alpha test.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = image.Sample(imageSampler, input.uv) * input.color;
    if (color.a <= input.alphaTest)
    {
        discard;
    }
    return color;
}

// The light of a lit sprite, which the blend multiplies what is there by.
[shader("fragment")]
float4 lightFragmentMain(VertexOutput input) : SV_Target
{
    return input.color;
}
