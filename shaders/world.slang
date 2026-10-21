// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The BSP world (nx.render:world): each face's texture, already through `texgamma`, times the light
// of its lightmap through `lightgamma`, `brightness` and `gamma`, as the original draws it
// (nx.render:gamma). Masked textures (`{`) discard their transparent texels.

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // x: what a sample, from 0 to 255, is multiplied by to index the light table.
    float4 lighting;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]]
Texture2D lightmaps;
[[vk::binding(2, 0)]]
SamplerState lightmapSampler;
// 1024 x 1: the light of each summed light, from 0 to 1023.
[[vk::binding(3, 0)]]
Texture2D lightTable;

[[vk::binding(0, 1)]]
Texture2D albedo;
[[vk::binding(1, 1)]]
SamplerState albedoSampler;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float2 lightmapUv : TEXCOORD1;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 lightmapUv : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    const float4 point = float4(input.position, 1.0);
    VertexOutput output;
    output.position = float4(dot(frame.rows[0], point), dot(frame.rows[1], point), dot(frame.rows[2], point),
                             dot(frame.rows[3], point));
    output.uv = input.uv;
    output.lightmapUv = input.lightmapUv;
    return output;
}

/// The light of `sample`, from 0 to 255: the original's summed light, truncated and held to 1023, read
/// from the light table.
float lightOf(float sample)
{
    const int index = min(int(sample * frame.lighting.x), 1023);
    return lightTable.Load(int3(index, 0, 0)).r;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = albedo.Sample(albedoSampler, input.uv);
    if (color.a < 0.5)
    {
        discard;
    }
    const float3 sample = lightmaps.Sample(lightmapSampler, input.lightmapUv).rgb * 255.0;
    const float3 light = float3(lightOf(sample.r), lightOf(sample.g), lightOf(sample.b));
    return float4(color.rgb * light, 1.0);
}
