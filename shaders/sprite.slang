// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Sprites (nx.render:sprite): each a quad the CPU turns as its sprite's orientation says, its image
// times its entity's color, as opaque as its render mode makes it, as the original's GL draws it
// (GL_MODULATE), and times its brightness, as light; a fragment whose alpha is not above the alpha
// test is discarded (GL_ALPHA_TEST). An alpha-tested sprite the world lights is drawn again where it
// was, its light multiplying what is there.
// Drawn as linear light (colorEncoding.slang), the images are sampled from sRGB as light, and the
// entity's color and the light as display colors made light too; how opaque, as it is.
// Each fragment is seen through the frame's fog (fog.slang), the light of a lit sprite as well, as the
// original's fog mixes every fragment's color before it is blended; but for those added (kRenderGlow,
// kRenderTransAdd), which its fog leaves as they are.

import colorEncoding;
import fog;

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // The fog the sprites are seen through.
    Fog fog;
    // How the colors reach the target: kDisplayEncoding or kLinearEncoding.
    uint colorEncoding;
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
    // What its color is multiplied by once it is light: 1, or more for a glow.
    [[vk::location(4)]] float brightness : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation float alphaTest : TEXCOORD1;
    nointerpolation float brightness : TEXCOORD2;
    // In the world, where the fog is measured from.
    float3 worldPosition : TEXCOORD3;
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
    output.brightness = input.brightness;
    output.worldPosition = input.position;
    return output;
}

// The image times the color and the brightness, cut by the alpha test.
float4 colorOf(VertexOutput input)
{
    const float4 texel = image.Sample(imageSampler, input.uv);
    const float3 light = encodedOf(input.color.rgb, frame.colorEncoding) * input.brightness;
    const float4 color = float4(texel.rgb * light, texel.a * input.color.a);
    if (color.a <= input.alphaTest)
    {
        discard;
    }
    return color;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = colorOf(input);
    return float4(foggedOf(color.rgb, frame.fog, input.worldPosition), color.a);
}

// An added sprite's, through no fog.
[shader("fragment")]
float4 addedFragmentMain(VertexOutput input) : SV_Target
{
    return colorOf(input);
}

// The light of a lit sprite, which the blend multiplies what is there by.
[shader("fragment")]
float4 lightFragmentMain(VertexOutput input) : SV_Target
{
    return float4(foggedOf(encodedOf(input.color.rgb, frame.colorEncoding), frame.fog, input.worldPosition),
                  input.color.a);
}
