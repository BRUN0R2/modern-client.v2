// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Studio models (nx.render:studio): each vertex placed by its bone of the pose the CPU computed
// (cs::poseBones), its normal turned by a bone of its own, and lit as the original lights a vertex
// (R_StudioLighting): its model's ambient light, and its shade as far as the normal faces against the
// light's way, through the original's light curve and times the light's color. The texture, already
// through `texgamma`, is drawn times that light. A chrome texture's coordinates come from the normal
// against the viewer (R_StudioSetupChrome); a masked texture is cut where its alpha is 0.5 or less;
// an additive one is added to what is behind. A model drawn other than normally is as opaque as its
// blend, an added one unlit, its light its blend; the pipelines blend it as its render mode says.

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // xyz: where the view is, which chrome is seen from.
    float4 eye;
    // xyz: the view's right, which chrome is turned by.
    float4 right;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;
// The original's lightgammatable: the light, from 0 to 1023, of each light from 0 to 1023.
[[vk::binding(1, 0)]]
StructuredBuffer<uint> lightGamma;
// The bones of every model drawn this frame, three rows each: a bone's transform from its space to
// the world.
[[vk::binding(2, 0)]]
StructuredBuffer<float4> bones;

[[vk::binding(0, 1)]]
Texture2D albedo;
[[vk::binding(1, 1)]]
SamplerState albedoSampler;

// The texture a mesh is drawn with: how its texels map to coordinates, and how it is drawn.
struct Skin
{
    // 1 / width and 1 / height.
    float2 texelSize;
    uint flags;
    uint reserved;
};

[[vk::binding(2, 1)]]
ConstantBuffer<Skin> skin;

// A model drawn: its light (the original's alight_t), how its depths are mapped, and where its bones
// start in `bones`.
struct Instance
{
    // xyz: where the light goes, unit length.
    float4 lightDirection;
    // xyz: its color, its brightest channel 1.
    float4 lightColor;
    float ambient;
    float shade;
    // Clip depth becomes depth * depthScale + w * depthOffset: 1 and 0, or for the view's own model
    // the nearest share of the depths.
    float depthScale;
    float depthOffset;
    uint firstBone;
    // How opaque the model is drawn, from 0 to 1.
    float blend;
    // 1: an added model, whose vertices' light is its blend.
    uint isUnlit;
};

[[vk::binding(0, 2)]]
ConstantBuffer<Instance> instance;

// Skin::flags, as nx.render:studio sets them.
static const uint kFlatShade = 1;
static const uint kChrome = 2;
static const uint kFullbright = 4;

static const uint kBoneRows = 3;
// What flat shading keeps of the shade.
static const float kFlatShadeShare = 0.8;
// The original's SHADE_LAMBERT: how far around a model its shade reaches.
static const float kLambert = 1.4953241;
// The most light a vertex takes; four times it indexes the light curve.
static const float kMostLight = 255.0;
static const float kLightCurveSteps = 4.0;
static const float kLightCurveTop = 1023.0;
// A channel of a vertex's color is a byte to the original.
static const float kChannelTop = 255.0;
// Chrome's coordinates span this many texels either way of their middle.
static const float kChromeHalfSpan = 32.0;

struct VertexInput
{
    // In the space of its bone.
    [[vk::location(0)]] float3 position : POSITION;
    // In the space of its own bone.
    [[vk::location(1)]] float3 normal : NORMAL;
    // In texels of the mesh's texture.
    [[vk::location(2)]] float2 texel : TEXCOORD0;
    [[vk::location(3)]] uint positionBone : TEXCOORD1;
    [[vk::location(4)]] uint normalBone : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    // rgb the vertex's light, a how opaque the model is: each a byte, as the original stores a vertex's
    // color.
    float4 light : COLOR0;
};

float4 boneRow(uint bone, uint row)
{
    return bones[((instance.firstBone + bone) * kBoneRows) + row];
}

float3 placedPoint(uint bone, float3 point)
{
    const float4 p = float4(point, 1.0);
    return float3(dot(boneRow(bone, 0), p), dot(boneRow(bone, 1), p), dot(boneRow(bone, 2), p));
}

float3 turnedDirection(uint bone, float3 direction)
{
    return float3(dot(boneRow(bone, 0).xyz, direction), dot(boneRow(bone, 1).xyz, direction),
                  dot(boneRow(bone, 2).xyz, direction));
}

float3 boneOrigin(uint bone)
{
    return float3(boneRow(bone, 0).w, boneRow(bone, 1).w, boneRow(bone, 2).w);
}

/// The light of a vertex whose normal is `normal`, from 0 to 1, as R_StudioLighting finds it: fully
/// bright, or the ambient light, and the shade the more the normal faces against the light's way
/// (its modified hemispherical lighting), through the light curve.
float lightOf(float3 normal, uint flags)
{
    if ((flags & kFullbright) != 0)
    {
        return 1.0;
    }
    float light = instance.ambient;
    if ((flags & kFlatShade) != 0)
    {
        light += instance.shade * kFlatShadeShare;
    }
    else
    {
        // -1 against the light's way, 1 along it.
        const float along = min(dot(normal, instance.lightDirection.xyz), 1.0);
        const float shadowed = (along + (kLambert - 1.0)) / kLambert;
        light += instance.shade;
        if (shadowed > 0.0)
        {
            light -= instance.shade * shadowed;
        }
        light = max(light, 0.0);
    }
    light = min(light, kMostLight);
    return float(lightGamma[uint(light * kLightCurveSteps)]) / kLightCurveTop;
}

/// The texture coordinates of chrome at a vertex whose normal, turned by `bone`, is `normal`: from
/// the viewer towards the bone, its up and right (R_StudioSetupChrome).
float2 chromeOf(uint bone, float3 normal)
{
    const float3 toBone = normalize(boneOrigin(bone) - frame.eye.xyz);
    const float3 up = normalize(cross(toBone, frame.right.xyz));
    const float3 right = normalize(cross(up, toBone));
    return float2(dot(normal, right) + 1.0, dot(normal, up) + 1.0) * kChromeHalfSpan;
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    const float4 position = float4(placedPoint(input.positionBone, input.position), 1.0);
    const float3 normal = turnedDirection(input.normalBone, input.normal);
    VertexOutput output;
    const float4 clip = float4(dot(frame.rows[0], position), dot(frame.rows[1], position),
                               dot(frame.rows[2], position), dot(frame.rows[3], position));
    output.position = float4(clip.xy, (clip.z * instance.depthScale) + (clip.w * instance.depthOffset), clip.w);
    const float2 texel = (skin.flags & kChrome) != 0 ? chromeOf(input.normalBone, normal) : input.texel;
    output.uv = texel * skin.texelSize;
    // An added model is unlit: its light is its blend (R_StudioDrawPoints).
    const float3 light =
        instance.isUnlit != 0 ? float3(instance.blend) : instance.lightColor.xyz * lightOf(normal, skin.flags);
    output.light = floor(float4(light, instance.blend) * kChannelTop) / kChannelTop;
    return output;
}

// Opaque and additive meshes: the texture times the light, its alpha times how opaque the model is.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return albedo.Sample(albedoSampler, input.uv) * input.light;
}

// The original's alpha test of masked meshes: what has an alpha above it is drawn.
static const float kMaskedAlphaTest = 0.5;

// Masked meshes: cut where the texture is transparent.
[shader("fragment")]
float4 maskedFragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = albedo.Sample(albedoSampler, input.uv) * input.light;
    if (color.a <= kMaskedAlphaTest)
    {
        discard;
    }
    return color;
}
