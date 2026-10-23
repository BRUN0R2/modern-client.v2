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
//
// The enhanced look (docs/adr/0025-enhanced-rendering.md) shades each texture by its material, in
// linear light: what the original's light gives a vertex lights its diffuse share, a metal reflects its
// surroundings (the sky, blurred as it is rough) by Karis's split-sum approximation, the lights of the
// map near it give highlights by GGX with height-correlated Smith visibility and Schlick's Fresnel,
// its roughness widened where its normal turns within a pixel so that thin edges do not shimmer, and
// what it emits is added beyond 1, which the bloom spreads. A material with nothing set draws as the
// original does.

import skyDirection;

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // xyz: where the view is, which chrome is seen from.
    float4 eye;
    // xyz: the view's right, which chrome is turned by.
    float4 right;
    // x: the last mip level of the environment, the roughest; y: 1 when it is the map's sky, 0 for none.
    float4 environment;
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
// What a metal reflects: the sides of the sky toward +X, -X, +Y, -Y, +Z and -Z, as the screen shows
// them, with their mip levels, the rougher the blurrier.
[[vk::binding(3, 0)]]
Texture2DArray environment;
[[vk::binding(4, 0)]]
SamplerState environmentSampler;

[[vk::binding(0, 1)]]
Texture2D albedo;
[[vk::binding(1, 1)]]
SamplerState albedoSampler;

// The texture a mesh is drawn with: how its texels map to coordinates, and how it is drawn; and its
// material, for the enhanced look.
struct Skin
{
    // 1 / width and 1 / height.
    float2 texelSize;
    uint flags;
    // From 0, a dielectric, to 1, a metal.
    float metallic;
    // From 0, a mirror, to 1.
    float roughness;
    // How bright what it emits is: its color, where emissiveMask says, times this; 1 is as bright as
    // white, beyond 1 it blooms.
    float emissive;
    float2 padding;
};

[[vk::binding(2, 1)]]
ConstantBuffer<Skin> skin;
// Where it emits, and how much: white everywhere when its material names no map.
[[vk::binding(3, 1)]]
Texture2D emissiveMask;

// The most lights a model lights itself by in the enhanced look.
static const uint kMostLights = 4;

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
    // The enhanced look's lights near the model, nearest first: xyz where each is, w how far it reaches;
    // and rgb its light, in linear light.
    uint lightCount;
    float4 lightPositions[kMostLights];
    float4 lightColors[kMostLights];
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
    // In the world, for the enhanced look.
    float3 worldPosition : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
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
    output.worldPosition = position.xyz;
    output.worldNormal = normal;
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

// --- The enhanced look -------------------------------------------------------------------------

static const float kPi = 3.14159265;
// The display's gamma the textures and the original's light are drawn for, which linear light is
// brought from and back to.
static const float kDisplayGamma = 2.2;
// How much light a dielectric reflects head on.
static const float kDielectricReflectance = 0.04;
// The smoothest a surface is shaded as: a mirror's highlight would be a single point.
static const float kLeastRoughness = 0.045;
// The most a normal's change across a pixel widens the roughness (Kaplanyan and Hoffmann).
static const float kMostKernelRoughness = 0.18;
// The least of a cosine, so that a grazing view divides by no zero.
static const float kLeastCosine = 1e-4;

float3 linearOf(float3 display)
{
    return pow(max(display, 0.0), kDisplayGamma);
}

float3 displayOf(float3 linear)
{
    return pow(max(linear, 0.0), 1.0 / kDisplayGamma);
}

float luminanceOf(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// What a surface of `roughness` reflects toward `direction`: the sky there, blurred as it is rough.
float3 environmentOf(float3 direction, float roughness)
{
    const SkyPoint point = skyPointOf(direction);
    const float level = roughness * frame.environment.x;
    return linearOf(environment.SampleLevel(environmentSampler, float3(skyUvOf(point), point.side), level).rgb);
}

// The share of its surroundings a surface reflects toward the viewer, Karis's fit of the split-sum's
// lookup ("Physically Based Shading on Mobile", 2014).
float3 environmentShareOf(float3 reflectance, float roughness, float nDotV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = (roughness * c0) + c1;
    const float a004 = (min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x) + r.y;
    const float2 ab = (float2(-1.04, 1.04) * a004) + r.zw;
    return (reflectance * ab.x) + ab.y;
}

// GGX's distribution of microfacet normals, `alphaSquared` the roughness to the fourth.
float distributionOf(float nDotH, float alphaSquared)
{
    const float d = (nDotH * nDotH * (alphaSquared - 1.0)) + 1.0;
    return alphaSquared / (kPi * d * d);
}

// Smith's visibility, height-correlated (Heitz 2014).
float visibilityOf(float nDotV, float nDotL, float alphaSquared)
{
    const float view = nDotL * sqrt((nDotV * nDotV * (1.0 - alphaSquared)) + alphaSquared);
    const float light = nDotV * sqrt((nDotL * nDotL * (1.0 - alphaSquared)) + alphaSquared);
    return 0.5 / max(view + light, kLeastCosine);
}

float3 fresnelOf(float3 reflectance, float vDotH)
{
    return reflectance + ((1.0 - reflectance) * pow(1.0 - vDotH, 5.0));
}

// The roughness to the fourth, widened by how much `normal` turns across the pixel (Kaplanyan and
// Hoffmann, "Stable Specular Highlights", 2016).
float filteredAlphaSquaredOf(float3 normal, float roughness)
{
    const float3 acrossX = ddx(normal);
    const float3 acrossY = ddy(normal);
    const float variance = 0.25 * (dot(acrossX, acrossX) + dot(acrossY, acrossY));
    const float alpha = roughness * roughness;
    return saturate((alpha * alpha) + min(2.0 * variance, kMostKernelRoughness));
}

// The highlight of light `index` on a surface.
float3 highlightOf(uint index, float3 position, float3 normal, float3 toViewer, float3 reflectance,
                   float alphaSquared)
{
    const float4 light = instance.lightPositions[index];
    const float3 toLight = light.xyz - position;
    const float distance = length(toLight);
    if (distance >= light.w || distance <= 0.0)
    {
        return float3(0.0);
    }
    const float3 l = toLight / distance;
    const float nDotL = saturate(dot(normal, l));
    if (nDotL <= 0.0)
    {
        return float3(0.0);
    }
    const float3 h = normalize(l + toViewer);
    const float nDotV = max(dot(normal, toViewer), kLeastCosine);
    // Its light fades with the square of the distance, to nothing where it stops reaching.
    const float reach = saturate(1.0 - pow(distance / light.w, 4.0));
    const float fade = (reach * reach) / max(distance * distance, 1.0);
    const float3 specular = distributionOf(saturate(dot(normal, h)), alphaSquared) *
                            visibilityOf(nDotV, nDotL, alphaSquared) * fresnelOf(reflectance, saturate(dot(toViewer, h)));
    return specular * instance.lightColors[index].rgb * fade * nDotL;
}

// `texel` of a mesh shaded by its material, as the screen shows it, beyond 1 where it is brighter
// than white.
float4 shadeEnhanced(VertexOutput input, float4 texel)
{
    const float3 base = linearOf(texel.rgb);
    const float3 emitted = base * skin.emissive * emissiveMask.Sample(albedoSampler, input.uv).rgb;
    // Chrome is the original's own reflection, and an added model is unlit: both as the original, and
    // what they emit.
    if ((skin.flags & kChrome) != 0 || instance.isUnlit != 0)
    {
        return float4(displayOf(linearOf(texel.rgb * input.light.rgb) + emitted), texel.a * input.light.a);
    }
    const float3 light = linearOf(input.light.rgb);
    const float3 normal = normalize(input.worldNormal);
    const float3 toViewer = normalize(frame.eye.xyz - input.worldPosition);
    const float nDotV = max(dot(normal, toViewer), kLeastCosine);
    const float roughness = max(skin.roughness, kLeastRoughness);
    const float3 reflectance = lerp(float3(kDielectricReflectance), base, skin.metallic);
    float3 color = base * (1.0 - skin.metallic) * light;
    // Its surroundings: the light the original gives it here, as from all around, and the sky as much as
    // the place is lit, so that a dark corner reflects its own dimness and the open the sky.
    const float openness = saturate(luminanceOf(light)) * frame.environment.y;
    const float3 surroundings = lerp(light, environmentOf(reflect(-toViewer, normal), roughness), openness);
    color += surroundings * environmentShareOf(reflectance, roughness, nDotV);
    const float alphaSquared = filteredAlphaSquaredOf(normal, roughness);
    for (uint index = 0; index < min(instance.lightCount, kMostLights); ++index)
    {
        color += highlightOf(index, input.worldPosition, normal, toViewer, reflectance, alphaSquared);
    }
    color += emitted;
    return float4(displayOf(color), texel.a * input.light.a);
}

[shader("fragment")]
float4 enhancedFragmentMain(VertexOutput input) : SV_Target
{
    return shadeEnhanced(input, albedo.Sample(albedoSampler, input.uv));
}

[shader("fragment")]
float4 enhancedMaskedFragmentMain(VertexOutput input) : SV_Target
{
    const float4 texel = albedo.Sample(albedoSampler, input.uv);
    if (texel.a * input.light.a <= kMaskedAlphaTest)
    {
        discard;
    }
    return shadeEnhanced(input, texel);
}
