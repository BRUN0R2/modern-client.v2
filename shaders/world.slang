// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The BSP world (nx.render:world): each face's texture, already through `texgamma`, times the light
// of its lightmap through `lightgamma`, `brightness` and `gamma`, as the original draws it
// (nx.render:gamma). Masked textures (`{`) discard their transparent texels. The sky's faces show
// the side of the sky they are seen toward, projected as the original projects it.

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // x: what a sample, from 0 to 255, is multiplied by to index the light table.
    float4 lighting;
    // xyz: the camera's position, where the directions to the sky start.
    float4 eye;
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
// The sides of the sky toward +X, -X, +Y, -Y, +Z and -Z, one layer each.
[[vk::binding(4, 0)]]
Texture2DArray skySides;
[[vk::binding(5, 0)]]
SamplerState skySampler;

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

float4 clipPositionOf(float3 position)
{
    const float4 point = float4(position, 1.0);
    return float4(dot(frame.rows[0], point), dot(frame.rows[1], point), dot(frame.rows[2], point),
                  dot(frame.rows[3], point));
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = clipPositionOf(input.position);
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

struct SkyVertexOutput
{
    float4 position : SV_Position;
    float3 direction : TEXCOORD0;
};

[shader("vertex")]
SkyVertexOutput skyVertexMain(VertexInput input)
{
    SkyVertexOutput output;
    output.position = clipPositionOf(input.position);
    output.direction = input.position - frame.eye.xyz;
    return output;
}

/// Where a direction reaches the sky: a side, and a point of it.
struct SkyPoint
{
    float side; // the layer: 0 to 5 for +X, -X, +Y, -Y, +Z and -Z
    float2 st;  // s to the right and t up, from -1 to 1
};

/// The side `direction` points at, by its largest axis with ties broken as the original breaks them,
/// and the point it reaches there, as the original projects it (vec_to_st): from the middle of the
/// sky, each side is seen upright and unmirrored, the top and bottom ones facing +X.
SkyPoint skyPointOf(float3 direction)
{
    const float3 size = abs(direction);
    SkyPoint point;
    if (size.x > size.y && size.x > size.z)
    {
        point.side = direction.x > 0.0 ? 0.0 : 1.0;
        point.st = float2(-direction.y * sign(direction.x), direction.z) / size.x;
    }
    else if (size.y > size.z && size.y > size.x)
    {
        point.side = direction.y > 0.0 ? 2.0 : 3.0;
        point.st = float2(direction.x * sign(direction.y), direction.z) / size.y;
    }
    else
    {
        // A tie of x and y with no z ends here, as in the original: the edge of the side, not 0 / 0.
        point.side = direction.z > 0.0 ? 4.0 : 5.0;
        point.st = float2(-direction.y, -direction.x * sign(direction.z)) / max(size.z, 1e-30);
    }
    return point;
}

[shader("fragment")]
float4 skyFragmentMain(SkyVertexOutput input) : SV_Target
{
    const SkyPoint point = skyPointOf(input.direction);
    const float2 uv = float2(point.st.x + 1.0, 1.0 - point.st.y) * 0.5; // texture v is down
    return float4(skySides.Sample(skySampler, float3(uv, point.side)).rgb, 1.0);
}