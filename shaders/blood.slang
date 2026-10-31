// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The enhanced look's blood (nx.render:blood): each drop a quad the shader makes of its instance's number,
// no vertex of the CPU's. A droplet, round, is drawn along the way it flies across the line of sight, as
// far as it flies in the frame's streak time, its head where it is: of its color times the light where it
// is, darker toward its rim, a glint near its head, its edge smoothed over a pixel. A mist is a puff of
// smoke, as thick as it is through its core and thinning to its edge, its density uneven by a noise its
// seed turns. A splat lies flat on what it landed on, a little off it, its edge uneven by its seed,
// darker where it pools at its middle. Drawn as linear light
// (colorEncoding.slang), a drop's display color is made light, then lit; all are seen through the frame's
// fog (fog.slang).

import colorEncoding;
import fog;

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // Where the view is, and its right and up; their fourth unused.
    float4 eye;
    float4 right;
    float4 up;
    // The fog the drops are seen through.
    Fog fog;
    // The streak time, in seconds; the others unused.
    float4 style;
    // How the colors reach the target: kDisplayEncoding or kLinearEncoding.
    uint colorEncoding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;

// A drop: where it is and its radius, its velocity and how opaque it is, its display color and the light
// where it is, its shape (kDroplet, kMist, kSplat), and a splat's normal and its seed, from 0 to 1.
struct Drop
{
    float4 positionRadius;
    float4 velocityOpacity;
    float4 colorLight;
    uint4 shape;
    float4 normalSeed;
};

[[vk::binding(1, 0)]]
StructuredBuffer<Drop> drops;

// nx::render::BloodShape's.
static const uint kDroplet = 0;
static const uint kMist = 1;
static const uint kSplat = 2;

// Below this speed across the line of sight, in units a second, a droplet is drawn round, facing the view.
static const float kStill = 1.0e-3;
// A droplet's light at its rim, of its light along its middle.
static const float kRimShade = 0.55;
// Its glint: where, toward its head and its left, in its radii from its head's middle; how wide, in its
// radii; and how much light it adds, of the drop's.
static const float2 kGlintAt = float2(-0.35, 0.35);
static const float kGlintWidth = 0.3;
static const float kGlintLight = 0.35;
// A splat: how far off what it lies on, in units, so that the surface does not hide it; its edge's
// lobes, as far in or out as this share of its radius, its quad reaching past them; and its light where
// it pools, at its middle, of its light at its edge.
static const float kSplatLift = 0.1;
static const float kLobeShare = 0.18;
// Its edge's two waves of lobes around it: how many lobes each makes, and its share of the lobes' reach.
static const float2 kLobeCounts = float2(5.0, 3.0);
static const float2 kLobeWeights = float2(0.6, 0.4);
static const float kSplatReach = 1.0 + kLobeShare;
static const float kPoolShade = 0.7;
// A mist: as thick as it is out to kMistCore of its radius, thinning to none at its edge; its smoke's
// density from kMistThinnest of it to all of it, by two waves of noise of kMistGrain cells across its
// radius, the second twice as fine and half as strong.
static const float kMistCore = 0.35;
static const float kMistThinnest = 0.4;
static const float kMistGrain = 2.5;
static const float kMistFinerGrain = 2.0;
static const float kMistFinerShare = 0.35;
// A turn, in radians, and how far apart, in noise cells, the seeds of two drops set their smoke.
static const float kFullTurn = 6.2831853;
static const float kSeedReach = 37.0;

// The corners of a quad, two triangles: across it (x) and along it (y), each from -1 to 1.
static const float2 kCorners[6] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
                                    float2(-1.0, -1.0), float2(1.0, 1.0),  float2(-1.0, 1.0) };

struct VertexOutput
{
    float4 position : SV_Position;
    // Where in its quad, across it and along it, in units from its middle.
    float2 local : TEXCOORD0;
    // In the world, where the fog is measured from.
    float3 worldPosition : TEXCOORD1;
    // Its half width and half length, in units.
    nointerpolation float2 halfSize : TEXCOORD2;
    // Its display color and light, how opaque it is, its shape, and where its splat's lobes turn.
    nointerpolation float4 colorLight : COLOR0;
    nointerpolation float opacity : OPACITY;
    nointerpolation uint shape : SHAPE;
    nointerpolation float seed : SEED;
};

float4 clipOf(float3 position)
{
    const float4 point = float4(position, 1.0);
    return float4(dot(frame.rows[0], point), dot(frame.rows[1], point), dot(frame.rows[2], point),
                  dot(frame.rows[3], point));
}

// A quad: its middle, the ways across it and along it, and its half width and half length.
struct Quad
{
    float3 middle;
    float3 side;
    float3 along;
    float2 halfSize;
};

// A droplet's or a mist's quad, facing the view; a droplet's drawn along the way it flies across the line
// of sight, its head where it is and its streak behind it.
Quad facingQuadOf(Drop drop)
{
    const float3 position = drop.positionRadius.xyz;
    const float radius = drop.positionRadius.w;
    const float3 toEye = frame.eye.xyz - position;
    const float eyeDistance = length(toEye);
    // At the eye, where none is seen: the view's own line.
    const float3 facing = eyeDistance > 0.0 ? toEye / eyeDistance : cross(frame.right.xyz, frame.up.xyz);
    const float3 velocity = drop.velocityOpacity.xyz;
    const float3 across = velocity - (facing * dot(velocity, facing));
    const float acrossSpeed = length(across);
    const bool isStreaked = drop.shape.x == kDroplet && acrossSpeed > kStill;
    const float streak = isStreaked ? acrossSpeed * frame.style.x : 0.0;
    Quad quad;
    quad.along = isStreaked ? across / acrossSpeed : frame.up.xyz;
    quad.side = isStreaked ? normalize(cross(quad.along, facing)) : frame.right.xyz;
    quad.middle = position - (quad.along * (streak * 0.5));
    quad.halfSize = float2(radius, radius + (streak * 0.5));
    return quad;
}

// A splat's quad, flat on what it lies on, a little off it, reaching past its lobes.
Quad splatQuadOf(Drop drop)
{
    const float3 normal = normalize(drop.normalSeed.xyz);
    const float3 aside = abs(normal.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    Quad quad;
    quad.side = normalize(cross(normal, aside));
    quad.along = cross(normal, quad.side);
    quad.middle = drop.positionRadius.xyz + (normal * kSplatLift);
    quad.halfSize = float2(drop.positionRadius.w * kSplatReach);
    return quad;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const Drop drop = drops[instance];
    const Quad quad = drop.shape.x == kSplat ? splatQuadOf(drop) : facingQuadOf(drop);
    const float2 local = kCorners[vertex] * quad.halfSize;
    const float3 point = quad.middle + (quad.side * local.x) + (quad.along * local.y);
    VertexOutput output;
    output.position = clipOf(point);
    output.local = local;
    output.worldPosition = point;
    output.halfSize = quad.halfSize;
    output.colorLight = drop.colorLight;
    output.opacity = drop.velocityOpacity.w;
    output.shape = drop.shape.x;
    output.seed = drop.normalSeed.w;
    return output;
}

// The light of a display color `display` lit by `light`, in the frame's target.
float3 litOf(float3 display, float light)
{
    return encodedOf(display, frame.colorEncoding) * light;
}

// How much of a pixel `edge`, a distance from a shape's edge (inside below 0), covers, smoothed over a pixel.
float coverageOf(float edge)
{
    const float pixel = max(fwidth(edge), 1.0e-4);
    return saturate(0.5 - (edge / pixel));
}

// A droplet: a round drop drawn along its flight, its two ends round.
float4 dropletOf(VertexOutput input)
{
    const float radius = input.halfSize.x;
    const float core = max(input.halfSize.y - radius, 0.0);
    // From the nearest point of its middle line, its head's middle at the top of the quad.
    const float2 off = float2(input.local.x, max(abs(input.local.y) - core, 0.0) * sign(input.local.y));
    const float coverage = coverageOf(length(off) - radius);
    if (coverage <= 0.0)
    {
        discard;
    }
    // Lit fully along its middle, as a round drop's front faces the view, darker toward its rim.
    const float depth = saturate(1.0 - (dot(off, off) / (radius * radius)));
    const float shade = lerp(kRimShade, 1.0, sqrt(depth));
    // A glint near its head, toward its left.
    const float2 head = float2(input.local.x, input.local.y - core);
    const float2 fromGlint = (head / radius) - kGlintAt;
    const float glint = kGlintLight * exp(-dot(fromGlint, fromGlint) / (kGlintWidth * kGlintWidth));
    const float light = input.colorLight.w;
    const float3 color = (litOf(input.colorLight.rgb, light) * shade) + (litOf(float3(1.0), light) * glint);
    return float4(color, coverage * input.opacity);
}

// The weights and scale of a hash of a noise cell to a share from 0 to 1.
static const float2 kHashWeights = float2(127.1, 311.7);
static const float kHashScale = 43758.5453;

float hashOf(float2 cell)
{
    return frac(sin(dot(cell, kHashWeights)) * kHashScale);
}

// A value noise at `point`, from 0 to 1: each cell's corner a share, eased between them.
float noiseOf(float2 point)
{
    const float2 cell = floor(point);
    const float2 within = frac(point);
    const float2 eased = within * within * (3.0 - (2.0 * within));
    const float below = lerp(hashOf(cell), hashOf(cell + float2(1.0, 0.0)), eased.x);
    const float above = lerp(hashOf(cell + float2(0.0, 1.0)), hashOf(cell + float2(1.0, 1.0)), eased.x);
    return lerp(below, above, eased.y);
}

// A mist: a puff of smoke, as thick as it is through its core, thinning to none at its edge, its density
// uneven by two waves of noise its seed sets, the same each frame.
float4 mistOf(VertexOutput input)
{
    const float2 within = input.local / input.halfSize.x;
    const float shape = 1.0 - smoothstep(kMistCore, 1.0, length(within));
    if (shape <= 0.0)
    {
        discard;
    }
    const float turn = input.seed * kFullTurn;
    const float2 offset = float2(cos(turn), sin(turn)) * kSeedReach;
    const float2 grain = (within * kMistGrain) + offset;
    const float smoke = lerp(noiseOf(grain), noiseOf((grain * kMistFinerGrain) + offset), kMistFinerShare);
    const float thickness = shape * lerp(kMistThinnest, 1.0, smoke);
    return float4(litOf(input.colorLight.rgb, input.colorLight.w), thickness * input.opacity);
}

// A splat: flat, its edge in and out by two waves of lobes around it, darker where it pools.
float4 splatOf(VertexOutput input)
{
    const float radius = input.halfSize.x / kSplatReach;
    const float reach = length(input.local) / radius;
    const float angle = atan2(input.local.y, input.local.x);
    const float lobeTurn = input.seed * kFullTurn;
    const float2 waves = sin((kLobeCounts * angle) + float2(lobeTurn, -lobeTurn));
    const float lobes = dot(waves, kLobeWeights);
    const float coverage = coverageOf((reach - (1.0 + (kLobeShare * lobes))) * radius);
    if (coverage <= 0.0)
    {
        discard;
    }
    const float shade = lerp(kPoolShade, 1.0, saturate(reach));
    return float4(litOf(input.colorLight.rgb, input.colorLight.w) * shade, coverage * input.opacity);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 color = input.shape == kMist    ? mistOf(input)
                         : input.shape == kSplat ? splatOf(input)
                                                 : dropletOf(input);
    return float4(foggedOf(color.rgb, frame.fog, input.worldPosition), color.a);
}
