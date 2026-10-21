// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The RHI's tests: a texture drawn over the whole target, one texel per pixel.

[[vk::binding(0, 0)]]
Texture2D image;
[[vk::binding(1, 0)]]
SamplerState pointSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// One triangle that covers the target: clip space (-1, -1), (3, -1), (-1, 3).
[shader("vertex")]
VertexOutput vertexMain(uint vertex : SV_VertexID)
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    VertexOutput output;
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    output.uv = float2(corner.x, 1.0 - corner.y); // clip space y is up, texture v is down
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return image.Sample(pointSampler, input.uv);
}
