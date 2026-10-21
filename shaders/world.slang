// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The BSP world (nx.render:world): each face's texture, already through `texgamma`, times its
// light, as the original draws it (nx.render:gamma): each luxel sums its lightmaps, each times its
// style's brightness, goes through `lightgamma`, `brightness` and `gamma`, and the face blends the
// four luxels around each point. Masked textures (`{`) discard their transparent texels; brush
// entities are drawn as their render mode says. The sky's faces show the side of the sky they are
// seen toward, projected as the original projects it.

// The styles the frame sets, four to an element of Frame::styles; the ones above keep the normal
// brightness, 'm'.
static const uint kSetStyles = 64;
static const uint kStylesPerElement = 4;

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // x: the original's lightscale; yz: the size of the lightmaps, in luxels; w: the time since the map
    // started, in seconds, which turbulent faces warp with.
    float4 lighting;
    // xyz: the camera's position, where the directions to the sky start.
    float4 eye;
    // The brightness of the light styles.
    float4 styles[kSetStyles / kStylesPerElement];
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;
// A layer for each of a face's lightmaps, at the same place in each.
[[vk::binding(1, 0)]]
Texture2DArray lightmaps;
// 1024 x 1: the light of each summed light, from 0 to 1023.
[[vk::binding(2, 0)]]
Texture2D lightTable;
// The sides of the sky toward +X, -X, +Y, -Y, +Z and -Z, one layer each.
[[vk::binding(3, 0)]]
Texture2DArray skySides;
[[vk::binding(4, 0)]]
SamplerState skySampler;

[[vk::binding(0, 1)]]
Texture2D albedo;
[[vk::binding(1, 1)]]
SamplerState albedoSampler;

// The brush model drawn: where the game puts it this frame, its turn (the rows of its rotation, in
// xyz) and its origin (xyz; the world's are the identity and 0); and, drawn other than normally, rgb
// its `rendercolor`, a its `renderamt`, from 0 to 1.
struct Entity
{
    float4 color;
    float4 origin;
    float4 turn[3];
};

float3 placedPosition(float3 position)
{
    return entity.origin.xyz +
           float3(dot(entity.turn[0].xyz, position), dot(entity.turn[1].xyz, position),
                  dot(entity.turn[2].xyz, position));
}
[[vk::binding(0, 2)]]
ConstantBuffer<Entity> entity;

static const uint kLightmaps = 4;
// The style of a lightmap the face does not have: its lightmaps end there.
static const uint kNoStyle = 255;
static const uint kNormalStyleValue = 264;
// A lightmap sample's channel, read from 0 to 1, is a byte to the original.
static const float kSampleTop = 255.0;
static const uint kLightScaleShift = 14;
static const uint kLightTableTop = 1023;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    // In luxels of the lightmaps; negative for a face without lightmaps.
    [[vk::location(2)]] float2 lightmapPosition : TEXCOORD1;
    [[vk::location(3)]] uint4 styles : TEXCOORD2;
    // 1 on a turbulent face (water, lasers): uv is in texels, and warps.
    [[vk::location(4)]] uint flags : TEXCOORD3;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 lightmapPosition : TEXCOORD1;
    nointerpolation uint4 styles : TEXCOORD2;
};

float4 clipPositionOf(float3 position)
{
    const float4 point = float4(position, 1.0);
    return float4(dot(frame.rows[0], point), dot(frame.rows[1], point), dot(frame.rows[2], point),
                  dot(frame.rows[3], point));
}

static const uint kTurbulent = 1;
static const float kTurbulentCell = 64.0;
// The original's warp: a table of kTurbulentSines sines of a turn, each times kTurbulentAmplitude
// texels, read kTurbulentFrequency times as fast as the texels go.
static const int kTurbulentSines = 256;
static const float kTurbulentAmplitude = 8.0;
static const float kTurbulentFrequency = 0.125;
static const float kTurn = 2.0 * 3.14159265358979;

/// The original's r_turbsin: kTurbulentAmplitude sin(2 pi i / kTurbulentSines), the values of its
/// warpsin table, the index wrapping around the table.
float turbulentSine(float position)
{
    const int index = int(position * (float(kTurbulentSines) / kTurn)) & (kTurbulentSines - 1);
    return kTurbulentAmplitude * sin(float(index) * (kTurn / float(kTurbulentSines)));
}

/// The texture coordinates of a vertex; on a turbulent face, its texels warped as the original's
/// EmitWaterPolys warps them at every corner of the pieces it cuts the face into, a cell of 64 texels
/// a texture.
float2 textureCoordinatesOf(VertexInput input)
{
    if (input.flags != kTurbulent)
    {
        return input.uv;
    }
    const float time = frame.lighting.w;
    const float s = input.uv.x + turbulentSine((input.uv.y * kTurbulentFrequency) + time);
    const float t = input.uv.y + turbulentSine((input.uv.x * kTurbulentFrequency) + time);
    return float2(s, t) / kTurbulentCell;
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = clipPositionOf(placedPosition(input.position));
    output.uv = textureCoordinatesOf(input);
    output.lightmapPosition = input.lightmapPosition;
    output.styles = input.styles;
    return output;
}

uint styleValueOf(uint style)
{
    return style < kSetStyles ? uint(frame.styles[style / kStylesPerElement][style % kStylesPerElement])
                              : kNormalStyleValue;
}

/// The light of `luxel`, from 0 to 1: the sum over the face's lightmaps of its sample times the
/// lightmap's style brightness, times lightscale and shifted, held to 1023 and read from the light
/// table, all in the original's integers (R_BuildLightMap).
float3 luxelLight(int2 luxel, uint4 styles)
{
    const int2 last = int2(frame.lighting.yz) - 1;
    const int2 inside = clamp(luxel, int2(0, 0), last);
    uint3 summed = uint3(0, 0, 0);
    for (uint map = 0; map < kLightmaps && styles[map] != kNoStyle; ++map)
    {
        const uint3 sample = uint3(round(lightmaps.Load(int4(inside, int(map), 0)).rgb * kSampleTop));
        summed += sample * styleValueOf(styles[map]);
    }
    const uint3 index = min((summed * uint(frame.lighting.x)) >> kLightScaleShift, kLightTableTop);
    return float3(lightTable.Load(int3(int(index.r), 0, 0)).r, lightTable.Load(int3(int(index.g), 0, 0)).r,
                  lightTable.Load(int3(int(index.b), 0, 0)).r);
}

/// The light at `position` (in luxels, their centers at halves): the four luxels around it blended,
/// as the original's bilinear filter blends its light texture. Fully bright without lightmaps, as the
/// original draws a face without samples.
float3 lightAt(float2 position, uint4 styles)
{
    if (position.x < 0.0)
    {
        return float3(1.0, 1.0, 1.0);
    }
    const float2 corner = position - 0.5;
    const int2 first = int2(floor(corner));
    const float2 weight = corner - float2(first);
    const float3 top = lerp(luxelLight(first, styles), luxelLight(first + int2(1, 0), styles), weight.x);
    const float3 bottom =
        lerp(luxelLight(first + int2(0, 1), styles), luxelLight(first + int2(1, 1), styles), weight.x);
    return lerp(top, bottom, weight.y);
}

// What a brush entity's `rendermode` makes of a fragment, as the original's R_SetRenderMode does: the
// world and normal entities, and the solid ones (kRenderTransAlpha), are lit and opaque, their masked
// (`{`) textures cut out by the original's alpha tests; the others are unlit and blended.

// The original's alpha tests: what has an alpha above them is drawn (DEFAULT_ALPHATEST, and
// kRenderTransAlpha's).
static const float kDefaultAlphaTest = 0.0;
static const float kSolidAlphaTest = 0.25;

float4 litOrDiscarded(VertexOutput input, float alphaTest)
{
    const float4 color = albedo.Sample(albedoSampler, input.uv);
    if (color.a <= alphaTest)
    {
        discard;
    }
    return float4(color.rgb * lightAt(input.lightmapPosition, input.styles), 1.0);
}

// The world and kRenderNormal: what has any alpha is drawn (the original's DEFAULT_ALPHATEST, 0).
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return litOrDiscarded(input, kDefaultAlphaTest);
}

// kRenderTransAlpha: what has an alpha above 0.25 is drawn.
[shader("fragment")]
float4 solidFragmentMain(VertexOutput input) : SV_Target
{
    return litOrDiscarded(input, kSolidAlphaTest);
}

// kRenderTransTexture and kRenderGlow: the texture, as opaque as renderamt makes it.
[shader("fragment")]
float4 textureFragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = albedo.Sample(albedoSampler, input.uv);
    return float4(color.rgb, color.a * entity.color.a);
}

// kRenderTransColor: rendercolor, as opaque as renderamt makes it, without the texture.
[shader("fragment")]
float4 colorFragmentMain(VertexOutput input) : SV_Target
{
    return entity.color;
}

// kRenderTransAdd: the texture times renderamt, added to what is behind.
[shader("fragment")]
float4 additiveFragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = albedo.Sample(albedoSampler, input.uv);
    return float4(color.rgb * entity.color.a, 1.0);
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
    float side; // the layer of skySides
    float2 st;  // s to the right and t up, from -1 to 1
};

// The layers of skySides, by the axis their side faces.
static const float kSkyPositiveX = 0.0;
static const float kSkyNegativeX = 1.0;
static const float kSkyPositiveY = 2.0;
static const float kSkyNegativeY = 3.0;
static const float kSkyPositiveZ = 4.0;
static const float kSkyNegativeZ = 5.0;
// The least a direction's largest axis is divided by: a direction of zero reaches an edge of a side.
static const float kLeastAxis = 1e-30;

/// The side `direction` points at, by its largest axis with ties broken as the original breaks them,
/// and the point it reaches there, as the original projects it (vec_to_st): from the middle of the
/// sky, each side is seen upright and unmirrored, the top and bottom ones facing +X.
SkyPoint skyPointOf(float3 direction)
{
    const float3 size = abs(direction);
    SkyPoint point;
    if (size.x > size.y && size.x > size.z)
    {
        point.side = direction.x > 0.0 ? kSkyPositiveX : kSkyNegativeX;
        point.st = float2(-direction.y * sign(direction.x), direction.z) / size.x;
    }
    else if (size.y > size.z && size.y > size.x)
    {
        point.side = direction.y > 0.0 ? kSkyPositiveY : kSkyNegativeY;
        point.st = float2(direction.x * sign(direction.y), direction.z) / size.y;
    }
    else
    {
        // A tie of x and y with no z ends here, as in the original: the edge of the side, not 0 / 0.
        point.side = direction.z > 0.0 ? kSkyPositiveZ : kSkyNegativeZ;
        point.st = float2(-direction.y, -direction.x * sign(direction.z)) / max(size.z, kLeastAxis);
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