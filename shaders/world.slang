// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The BSP world (nx.render:world): each face's texture lit by its lightmap. Masked textures (`{`)
// discard their transparent texels. The GoldSrc gamma curves come with the gamma pass.

struct Camera
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
};

[[vk::binding(0, 0)]]
ConstantBuffer<Camera> camera;
[[vk::binding(1, 0)]]
Texture2D lightmaps;
[[vk::binding(2, 0)]]
SamplerState lightmapSampler;

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
    output.position = float4(dot(camera.rows[0], point), dot(camera.rows[1], point), dot(camera.rows[2], point),
                             dot(camera.rows[3], point));
    output.uv = input.uv;
    output.lightmapUv = input.lightmapUv;
    return output;
}

/// GoldSrc's lightmaps light a texture up to twice its brightness (overbright).
static const float kOverbright = 2.0;

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = albedo.Sample(albedoSampler, input.uv);
    if (color.a < 0.5)
    {
        discard;
    }
    const float3 light = lightmaps.Sample(lightmapSampler, input.lightmapUv).rgb;
    return float4(saturate(color.rgb * light * kOverbright), 1.0);
}
