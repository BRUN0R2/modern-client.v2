// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The BSP world (nx.render:world): each face's texture, already through `texgamma`, times its
// light, as the original draws it (nx.render:gamma): each luxel sums its lightmaps, each times its
// style's brightness, goes through `lightgamma`, `brightness` and `gamma`, and the face blends the
// four luxels around each point. Masked textures (`{`) discard their transparent texels; brush
// entities are drawn as their render mode says, the water of those with waves rising and falling;
// decals lie over the faces, lit as they are, those of a brush entity drawn otherwise as its mode
// draws them.
// The sky's faces show the side of the sky they are seen toward, projected as the original projects
// it (skyDirection.slang).
// Drawn as linear light (colorEncoding.slang), the textures are sampled from sRGB as light, and the
// light and colors the original gives as display colors are made light too; how opaque, as it is. The
// decals' images stay the original's display colors, blended as the original blends them.

import colorEncoding;
import skyDirection;

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
    // How the colors reach the target: kDisplayEncoding or kLinearEncoding.
    uint colorEncoding;
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
// xyz) and its origin (xyz; the world's are the identity and 0), and in origin.w the height of its
// waves (0 for none); and, drawn other than normally, rgb its `rendercolor`, a its `renderamt`, from
// 0 to 1.
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
    // On a turbulent face, the height of the corner that says which way its waves go.
    [[vk::location(5)]] float waveBase : TEXCOORD4;
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

/// The original's r_turbsin at `index`: kTurbulentAmplitude sin(2 pi i / kTurbulentSines), the values
/// of its warpsin table, the index wrapping around the table.
float turbulentSineAt(int index)
{
    return kTurbulentAmplitude * sin(float(index & (kTurbulentSines - 1)) * (kTurn / float(kTurbulentSines)));
}

/// r_turbsin at `position` turns of the table, as the warp of the texels reads it.
float turbulentSine(float position)
{
    return turbulentSineAt(int(position * (float(kTurbulentSines) / kTurn)));
}

// The waves of a brush entity's turbulent faces: two sines of the table, each lifted by its amplitude
// above 0, the first along x + y kWaveSpeed entries a second, the second along kWaveStretch x - y
// kSecondWaveSpeed entries a second, kSecondWaveShare of it.
static const float kWaveSpeed = 160.0;
static const float kSecondWaveSpeed = 171.0;
static const float kWaveStretch = 5.0;
static const float kSecondWaveShare = 0.8;

/// How far the waves of the brush entity drawn raise a vertex of a turbulent face, in its model's
/// space, as the original's EmitWaterPolys raises them: by the entity's wave height, the other way
/// when the face's base is at or above the eye.
float waveRiseOf(VertexInput input)
{
    const float height = entity.origin.w;
    if (input.flags != kTurbulent || height == 0.0)
    {
        return 0.0;
    }
    const float time = frame.lighting.w;
    const float3 position = input.position;
    const float first =
        turbulentSineAt(int((time * kWaveSpeed) + position.y + position.x)) + kTurbulentAmplitude;
    const float second =
        (turbulentSineAt(int((position.x * kWaveStretch) + (time * kSecondWaveSpeed) - position.y)) +
         kTurbulentAmplitude) *
        kSecondWaveShare;
    const float way = input.waveBase >= frame.eye.z ? -1.0 : 1.0;
    return (first + second) * height * way;
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
    output.position = clipPositionOf(placedPosition(input.position + float3(0.0, 0.0, waveRiseOf(input))));
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
    return float4(color.rgb * encodedOf(lightAt(input.lightmapPosition, input.styles), frame.colorEncoding), 1.0);
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

// A decal over the face it lies on (nx.render:decals): its image's texel, a display color as the
// original makes it, laid over the face as opaque as its alpha. The original blends it over the face's
// display color w: c a + w (1 - a). A decal of the world or of a brush entity drawn normally or solid
// (kRenderTransAlpha) is lit as the face is there: the original draws its decals over the face's
// texture and then multiplies both by the lightmap; lit first, they come out the same. The decals of a
// brush entity drawn otherwise are unlit as its faces are, each texel times the color the original's
// R_SetRenderMode leaves set for its faces.
struct DecalCover
{
    float3 color;
    float opacity;
};

// The world's and kRenderNormal's and kRenderTransAlpha's: the texel, as opaque as its alpha.
DecalCover litDecalCoverOf(VertexOutput input)
{
    const float4 texel = albedo.Sample(albedoSampler, input.uv);
    return { texel.rgb, texel.a };
}

// kRenderTransTexture and kRenderGlow: the texel, as opaque as renderamt makes it.
DecalCover textureDecalCoverOf(VertexOutput input)
{
    const float4 texel = albedo.Sample(albedoSampler, input.uv);
    return { texel.rgb, texel.a * entity.color.a };
}

// kRenderTransColor: the texel times rendercolor, as opaque as renderamt makes it.
DecalCover colorDecalCoverOf(VertexOutput input)
{
    const float4 texel = albedo.Sample(albedoSampler, input.uv);
    return { texel.rgb * entity.color.rgb, texel.a * entity.color.a };
}

// kRenderTransAdd: the texel times renderamt, blended rather than added.
DecalCover additiveDecalCoverOf(VertexOutput input)
{
    const float4 texel = albedo.Sample(albedoSampler, input.uv);
    return { texel.rgb * entity.color.a, texel.a };
}

// Drawn as display colors (kDisplayEncoding), a decal is blended as the original blends it: its
// color, as opaque as its alpha.

[shader("fragment")]
float4 decalFragmentMain(VertexOutput input) : SV_Target
{
    const DecalCover cover = litDecalCoverOf(input);
    return float4(cover.color * lightAt(input.lightmapPosition, input.styles), cover.opacity);
}

[shader("fragment")]
float4 textureDecalFragmentMain(VertexOutput input) : SV_Target
{
    const DecalCover cover = textureDecalCoverOf(input);
    return float4(cover.color, cover.opacity);
}

[shader("fragment")]
float4 colorDecalFragmentMain(VertexOutput input) : SV_Target
{
    const DecalCover cover = colorDecalCoverOf(input);
    return float4(cover.color, cover.opacity);
}

[shader("fragment")]
float4 additiveDecalFragmentMain(VertexOutput input) : SV_Target
{
    const DecalCover cover = additiveDecalCoverOf(input);
    return float4(cover.color, cover.opacity);
}

// Drawn as linear light (kLinearEncoding), the original's blend is a power of the display's gamma of
// it, (c a + w (1 - a))^2.2, which no blend of the light under the decal by one factor gives: blended
// as light with its alpha, a mark half opaque would keep about twice the light of the face that the
// original keeps, and the dark ones, a bullet hole or blood, fade. So a decal gives the blend two colors
// (dual-source blending), what it adds and the share it keeps of the light under it, channel by
// channel: (c a)^2.2 and (1 - a + c a)^2.2 - (c a)^2.2. The face's light is the light of the face's
// display color w, w^2.2, so that the blend is the original's exactly over a black face, over a white
// one, and for a black texel over any face (a bullet hole); between them, a little darker (the game's
// blood on its red channel: by 2 of 255 on average, 16 at most). A lit decal's light multiplies the
// first, as the face's is already in the light the second keeps.
struct DisplayBlend
{
    [[vk::location(0), vk::index(0)]] float4 added : SV_Target0;
    [[vk::location(0), vk::index(1)]] float4 kept : SV_Target1;
};

DisplayBlend displayBlendOf(DecalCover cover, float3 light)
{
    const float3 covered = cover.color * cover.opacity;
    const float3 added = linearOf(covered);
    DisplayBlend blend;
    blend.added = float4(added * light, cover.opacity);
    blend.kept = float4(linearOf((1.0 - cover.opacity) + covered) - added, 0.0);
    return blend;
}

static const float3 kUnlit = float3(1.0, 1.0, 1.0);

[shader("fragment")]
DisplayBlend linearDecalFragmentMain(VertexOutput input)
{
    return displayBlendOf(litDecalCoverOf(input), linearOf(lightAt(input.lightmapPosition, input.styles)));
}

[shader("fragment")]
DisplayBlend linearTextureDecalFragmentMain(VertexOutput input)
{
    return displayBlendOf(textureDecalCoverOf(input), kUnlit);
}

[shader("fragment")]
DisplayBlend linearColorDecalFragmentMain(VertexOutput input)
{
    return displayBlendOf(colorDecalCoverOf(input), kUnlit);
}

[shader("fragment")]
DisplayBlend linearAdditiveDecalFragmentMain(VertexOutput input)
{
    return displayBlendOf(additiveDecalCoverOf(input), kUnlit);
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
    return float4(encodedOf(entity.color.rgb, frame.colorEncoding), entity.color.a);
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

[shader("fragment")]
float4 skyFragmentMain(SkyVertexOutput input) : SV_Target
{
    const SkyPoint point = skyPointOf(input.direction);
    return float4(skySides.Sample(skySampler, float3(skyUvOf(point), point.side)).rgb, 1.0);
}
