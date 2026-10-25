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
// The enhanced look (docs/adr/0025-enhanced-rendering.md) shades each texture by its surface, in linear
// light. A texture without one is drawn as the original draws it. A metallic-roughness surface, glTF
// 2.0's, is a dielectric lit by the original's light or a metal whose color is what it reflects. A
// painted surface's texels are a painting with its light painted in, a dark steel painted dark, a gold
// with its shine: the painting stays, lit by the original's light and by the light painted for it by
// direction, and the reflection its material gives is added over it. What a surface reflects is the map
// from its two nearest probes, blended, or the sky, times GGX's reflection integrated over the
// directions it gathers (the split sum); the lights of the map near it give highlights by GGX with
// height-correlated Smith visibility and Schlick's Fresnel, the roughness widened where the normal turns
// within a pixel so that thin edges do not shimmer; and what a texture emits is added beyond 1, which
// the bloom spreads.

import colorEncoding;
import numbers;
import skyDirection;

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // xyz: where the view is, which chrome is seen from.
    float4 eye;
    // xyz: the view's right, which chrome is turned by.
    float4 right;
    // x: the last mip level of the environment, the roughest; y: 1 when it is the map's sky, 0 for none;
    // z: the last mip level of the probes.
    float4 environment;
    // How the colors reach the target: kDisplayEncoding or kLinearEncoding.
    uint colorEncoding;
    // How the models are shaded: kLegacyLook or kEnhancedLook.
    uint look;
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
// The map's reflection probes (nx.render:probes): layer probe * 6 + side, laid out as the sky's.
[[vk::binding(5, 0)]]
Texture2DArray probes;
// GGX's reflection integrated over the directions it gathers, the split sum's second factor (Filament,
// section 5.3.4.3): red the share of the reflectance head on, green what grazing angles add; across the
// cosine of the view to the normal, down the roughness.
[[vk::binding(6, 0)]]
Texture2D specularIntegral;

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
    // How the enhanced look shades it: kOriginalSurface, kMetallicRoughnessSurface or kPaintedSurface.
    uint surface;
    // From 0, a dielectric, to 1, a metal: a metallic-roughness surface's.
    float metallic;
    // From 0, a mirror, to 1.
    float roughness;
    // How bright what it emits is: its color, where emissiveMask says, times this; 1 is as bright as
    // white, beyond 1 it blooms.
    float emissive;
    // 1: specularMap gives a painted surface's reflection by texel.
    uint hasSpecularMap;
    // A painted surface's reflection: xyz its color head on, in linear light; w how much of it is added.
    float4 specular;
    // 1: paintedLight lights a painted surface's painting by direction.
    uint hasPaintedLight;
    // That light's scale, in linear light.
    float paintedLightGain;
};

[[vk::binding(2, 1)]]
ConstantBuffer<Skin> skin;
// Where it emits, and how much: white everywhere when its material names no map.
[[vk::binding(3, 1)]]
Texture2D emissiveMask;
// A painted surface's reflection by texel: rgb its color head on, alpha times how much of it is added;
// data, not display colors.
[[vk::binding(4, 1)]]
Texture2D specularMap;
// The light painted for a painted surface by the direction it reflects, all around, equirectangular:
// across, the longitude atan2(y, x) / 2pi + 1/2; down, the latitude acos(z) / pi; sampled as light.
[[vk::binding(5, 1)]]
Texture2D paintedLight;

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
    // The reflection probes it reflects: the first, -1 for none (the sky then); the second, and how
    // much of it, the rest the first's.
    int probe;
    int secondProbe;
    float secondProbeShare;
};

[[vk::binding(0, 2)]]
ConstantBuffer<Instance> instance;

// Skin::flags, as nx.render:studio sets them.
static const uint kFlatShade = 1;
static const uint kChrome = 2;
static const uint kFullbright = 4;
// Frame::look, as nx.render:studio sets it (StudioLook).
static const uint kLegacyLook = 0;
static const uint kEnhancedLook = 1;
// Skin::surface, as nx.render:studio sets it: none, StudioMetallicRoughness or StudioPainted.
static const uint kOriginalSurface = 0;
static const uint kMetallicRoughnessSurface = 1;
static const uint kPaintedSurface = 2;

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
    // The enhanced look lights a fullbright texture wholly, the light's color left out. An added model
    // is unlit: its light is its blend (R_StudioDrawPoints); drawn as linear light, the blend scales the
    // light it adds instead (shadeLegacy, outputOf).
    const bool isWhollyLit = frame.look == kEnhancedLook && (skin.flags & kFullbright) != 0;
    const float3 lit = isWhollyLit ? float3(1.0) : instance.lightColor.xyz * lightOf(normal, skin.flags);
    const float unlit = frame.colorEncoding == kLinearEncoding ? 1.0 : instance.blend;
    const float3 light = instance.isUnlit != 0 ? float3(unlit) : lit;
    output.light = floor(float4(light, instance.blend) * kChannelTop) / kChannelTop;
    output.worldPosition = position.xyz;
    output.worldNormal = normal;
    return output;
}

// The texture times its light, its alpha times how opaque the model is, as the original draws it;
// drawn as linear light, the light made light, an added model's blend scaling what it adds.
float4 shadeLegacy(VertexOutput input)
{
    const float4 texel = albedo.Sample(albedoSampler, input.uv);
    if (frame.colorEncoding == kLinearEncoding)
    {
        const float amount = instance.isUnlit != 0 ? input.light.a : 1.0;
        return float4(texel.rgb * linearOf(input.light.rgb) * amount, texel.a * input.light.a);
    }
    return texel * input.light;
}

// Opaque and additive meshes: the texture times the light, its alpha times how opaque the model is.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return shadeLegacy(input);
}

// The original's alpha test of masked meshes: what has an alpha above it is drawn.
static const float kMaskedAlphaTest = 0.5;

// Masked meshes: cut where the texture is transparent.
[shader("fragment")]
float4 maskedFragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = shadeLegacy(input);
    if (color.a <= kMaskedAlphaTest)
    {
        discard;
    }
    return color;
}

// --- The enhanced look -------------------------------------------------------------------------

// How much light a dielectric reflects head on.
static const float kDielectricReflectance = 0.04;
// The smoothest a surface is shaded as: a mirror's highlight would be a single point.
static const float kLeastRoughness = 0.045;
// The most a normal's change across a pixel widens the roughness (Kaplanyan and Hoffmann).
static const float kMostKernelRoughness = 0.18;
// The least of a cosine, so that a grazing view divides by no zero.
static const float kLeastCosine = 1e-4;

// What a surface of `roughness` reflects toward `direction`: the sky there, blurred as it is rough, as
// light; its texture holds display colors unless the colors are drawn as linear light.
float3 environmentOf(float3 direction, float roughness)
{
    const SkyPoint point = skyPointOf(direction);
    const float level = roughness * frame.environment.x;
    const float3 sampled =
        environment.SampleLevel(environmentSampler, float3(skyUvOf(point), point.side), level).rgb;
    return frame.colorEncoding == kLinearEncoding ? sampled : linearOf(sampled);
}

// What a surface of `roughness` reflects toward `direction` from probe `probe`: the map there, blurred
// as it is rough.
float3 probeOf(uint probe, float3 direction, float roughness)
{
    const SkyPoint point = skyPointOf(direction);
    const float layer = float(probe * 6) + point.side;
    const float level = roughness * frame.environment.z;
    return probes.SampleLevel(environmentSampler, float3(skyUvOf(point), layer), level).rgb;
}

// What the model's two probes show toward `direction`, blurred as a surface of `roughness` reflects them,
// blended as they are shared.
float3 probesOf(float3 direction, float roughness)
{
    const float3 first = probeOf(uint(instance.probe), direction, roughness);
    if (instance.secondProbeShare <= 0.0)
    {
        return first;
    }
    return lerp(first, probeOf(uint(instance.secondProbe), direction, roughness), instance.secondProbeShare);
}

// What a surface of `roughness` and `reflectance` reflects toward the viewer of its surroundings: the
// map from its two nearest probes, or the sky, toward `reflected`, blurred as it is rough, times GGX's
// reflection integrated (specularIntegral), as Filament's sections 5.3.4 and 9.5 put them together;
// nothing without either.
float3 reflectionOf(float3 reflected, float roughness, float nDotV, float3 reflectance)
{
    const bool hasProbe = instance.probe >= 0;
    if (!hasProbe && frame.environment.y <= 0.0)
    {
        return float3(0.0);
    }
    const float3 seen = hasProbe ? probesOf(reflected, roughness) : environmentOf(reflected, roughness);
    const float2 integrated = specularIntegral.SampleLevel(environmentSampler, float2(nDotV, roughness), 0.0).rg;
    return seen * ((reflectance * integrated.x) + integrated.y);
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
                            visibilityOf(nDotV, nDotL, alphaSquared) *
                            fresnelOf(reflectance, saturate(dot(toViewer, h)));
    return specular * instance.lightColors[index].rgb * fade * nDotL;
}

// `light` toward the viewer as the target takes it, as opaque as `opacity`: linear light, an added
// model's blend scaling what it adds, as its pipeline adds it whole; or a display color.
float4 outputOf(float3 light, float opacity)
{
    if (frame.colorEncoding == kLinearEncoding)
    {
        return float4(light * (instance.isUnlit != 0 ? opacity : 1.0), opacity);
    }
    return float4(displayOf(light), opacity);
}

// The light painted for a painted surface toward `direction` (paintedLight), times its gain. The
// longitude's gradients wrap, so that its seam picks no blurrier level; the latitude stops half a texel
// short of the poles, so that it never wraps there.
float3 paintedLightOf(float3 direction)
{
    const float2 uv = float2((atan2(direction.y, direction.x) / (2.0 * kPi)) + 0.5,
                             acos(clamp(direction.z, -1.0, 1.0)) / kPi);
    float2 gradientX = ddx(uv);
    float2 gradientY = ddy(uv);
    gradientX.x -= round(gradientX.x);
    gradientY.x -= round(gradientY.x);
    uint width;
    uint height;
    paintedLight.GetDimensions(width, height);
    const float halfTexel = 0.5 / float(height);
    const float2 sampled = float2(uv.x, clamp(uv.y, halfTexel, 1.0 - halfTexel));
    return paintedLight.SampleGrad(albedoSampler, sampled, gradientX, gradientY).rgb * skin.paintedLightGain;
}

// What a surface gives toward the viewer before its reflection, its painting or its color lit; and its
// reflection's color head on, and how much of the reflection is added.
struct SurfaceShading
{
    float3 lit;
    float3 reflectance;
    float reflectionWeight;
};

// A metallic-roughness surface (glTF 2.0) of color `base` under `light`: a dielectric's color lit, a
// metal's none, its color being what it reflects.
SurfaceShading metallicRoughnessOf(float3 base, float3 light)
{
    SurfaceShading shading;
    shading.lit = base * light * (1.0 - skin.metallic);
    shading.reflectance = lerp(float3(kDielectricReflectance), base, skin.metallic);
    shading.reflectionWeight = 1.0;
    return shading;
}

// A painted surface: its painting `base` lit by `light` and, when it has it, by its painted light
// toward `reflected`; its reflection as its material gives it, by texel with a specular map.
SurfaceShading paintedOf(VertexOutput input, float3 base, float3 light, float3 reflected)
{
    const float4 specular =
        skin.hasSpecularMap != 0 ? specularMap.Sample(albedoSampler, input.uv) : float4(skin.specular.rgb, 1.0);
    SurfaceShading shading;
    shading.lit = base * light;
    if (skin.hasPaintedLight != 0)
    {
        shading.lit *= paintedLightOf(reflected);
    }
    shading.reflectance = specular.rgb;
    shading.reflectionWeight = skin.specular.w * specular.a;
    return shading;
}

// `texel` of a mesh shaded by its surface, as the target takes it; its color already light when the
// colors are drawn as linear light, sampled from sRGB.
float4 shadeEnhanced(VertexOutput input, float4 texel)
{
    const float3 base = frame.colorEncoding == kLinearEncoding ? texel.rgb : linearOf(texel.rgb);
    const float3 emitted = base * skin.emissive * emissiveMask.Sample(albedoSampler, input.uv).rgb;
    const float opacity = texel.a * input.light.a;
    // Chrome is the original's own reflection, an added model is unlit, and a texture without a surface
    // is shaded as the original shades it: each as the original draws it, and what it emits.
    if ((skin.flags & kChrome) != 0 || instance.isUnlit != 0 || skin.surface == kOriginalSurface)
    {
        if (skin.emissive <= 0.0 && frame.colorEncoding == kDisplayEncoding)
        {
            return texel * input.light;
        }
        return outputOf((base * linearOf(input.light.rgb)) + emitted, opacity);
    }
    const float3 light = linearOf(input.light.rgb);
    const float3 normal = normalize(input.worldNormal);
    const float3 toViewer = normalize(frame.eye.xyz - input.worldPosition);
    const float3 reflected = reflect(-toViewer, normal);
    SurfaceShading shading;
    if (skin.surface == kPaintedSurface)
    {
        shading = paintedOf(input, base, light, reflected);
    }
    else
    {
        shading = metallicRoughnessOf(base, light);
    }
    // Its roughness widened across the pixel here, where every pixel takes the same way.
    const float roughness = max(skin.roughness, kLeastRoughness);
    const float alphaSquared = filteredAlphaSquaredOf(normal, roughness);
    float3 color = shading.lit;
    if (shading.reflectionWeight > 0.0)
    {
        const float nDotV = max(dot(normal, toViewer), kLeastCosine);
        color += shading.reflectionWeight * reflectionOf(reflected, roughness, nDotV, shading.reflectance);
        for (uint index = 0; index < min(instance.lightCount, kMostLights); ++index)
        {
            color += shading.reflectionWeight *
                     highlightOf(index, input.worldPosition, normal, toViewer, shading.reflectance, alphaSquared);
        }
    }
    color += emitted;
    return outputOf(color, opacity);
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
