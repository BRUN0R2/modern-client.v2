// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The overlay (nx.render:overlay): triangles and lines in the map's space, each vertex of its own display
// color, as opaque as its alpha; drawn as linear light (colorEncoding.slang), its color made light times
// its brightness, which a display target leaves out.

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

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float brightness : BRIGHTNESS;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float brightness : BRIGHTNESS;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    const float4 point = float4(input.position, 1.0);
    VertexOutput output;
    output.position = float4(dot(frame.rows[0], point), dot(frame.rows[1], point), dot(frame.rows[2], point),
                             dot(frame.rows[3], point));
    output.color = input.color;
    output.brightness = input.brightness;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float gain = frame.colorEncoding == kLinearEncoding ? input.brightness : 1.0;
    return float4(encodedOf(input.color.rgb, frame.colorEncoding) * gain, input.color.a);
}
