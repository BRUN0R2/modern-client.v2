// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The overlay (nx.render:overlay). Its kept mesh: triangles and lines in the map's space, each vertex of
// its own display color, as opaque as its alpha. Its arrows, as the original's beam draws
// sprites/arrow1.spr (TE_BEAMPOINTS; R_DrawSegs as Xash3D-FWGS reconstructs it, GPL, consulted: docs/20
// section 3): for each stretch of a way, a band facing the view about the stretch, on which an arrow is
// drawn every spacing back from the stretch's end, pointing to it; each arrow the sprite's shape, drawn
// here and smoothed over a pixel: a head, brightest at its tip, then a shaft that fades to its tail, each of
// its own color. Both are drawn as linear light in a target of it (colorEncoding.slang), their color made
// light times their brightness, which a display target leaves out.

import colorEncoding;

struct Frame
{
    // The rows of the view-projection matrix: clip = (dot(row0, p), dot(row1, p), ...).
    float4 rows[4];
    // Where the view is; its fourth unused.
    float4 eye;
    // The arrows' band's width and their spacing, in units; the others unused.
    float4 arrowStyle;
    // How the colors reach the target: kDisplayEncoding or kLinearEncoding.
    uint colorEncoding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;

// A stretch of a way: its start, its end and its brightness, and the display colors of its arrows' heads
// and shafts.
struct Arrows
{
    float4 start;
    float4 endBrightness;
    float4 headColor;
    float4 shaftColor;
};

[[vk::binding(1, 0)]]
StructuredBuffer<Arrows> arrows;

// An arrow as sprites/arrow1.spr draws it, two arrows of 32 rows in its 16 by 64 texels, its rows along the
// band: from the front of its spacing back, its tip at row 7, its head's back at row 15 and its shaft's
// tail at row 23; its head as wide as 10 of the band's 16 texels at its back, its shaft as 7; its light
// from 1 at its tip to 0.8 at its head's back, then from 0.7 down to 0.2 at its shaft's tail.
static const float kTipAt = 7.0 / 32.0;
static const float kHeadBackAt = 15.0 / 32.0;
static const float kTailAt = 23.0 / 32.0;
static const float kHeadHalfWidth = 10.0 / 16.0;
static const float kShaftHalfWidth = 7.0 / 16.0;
static const float kTipLight = 1.0;
static const float kHeadBackLight = 0.8;
static const float kShaftLight = 0.7;
static const float kTailLight = 0.2;

float4 clipOf(float3 position)
{
    const float4 point = float4(position, 1.0);
    return float4(dot(frame.rows[0], point), dot(frame.rows[1], point), dot(frame.rows[2], point),
                  dot(frame.rows[3], point));
}

// The light a color of `display` and `brightness` gives in the frame's target.
float3 lightOf(float3 display, float brightness)
{
    const float gain = frame.colorEncoding == kLinearEncoding ? brightness : 1.0;
    return encodedOf(display, frame.colorEncoding) * gain;
}

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float brightness : BRIGHTNESS;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float brightness : BRIGHTNESS;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = clipOf(input.position);
    output.color = input.color;
    output.brightness = input.brightness;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(lightOf(input.color.rgb, input.brightness), input.color.a);
}

// The corners of a band, two triangles: along it from its start (0) to its end (1), across it from one
// side (-1) to the other (1).
static const float2 kBandCorners[6] = { float2(0.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
                                        float2(0.0, -1.0), float2(1.0, 1.0),  float2(0.0, 1.0) };

struct ArrowOutput
{
    float4 position : SV_Position;
    // How far back from the stretch's end, and across the band from its middle, in units.
    float2 band : TEXCOORD0;
    // The stretch's, the same at each corner.
    nointerpolation float4 headColor : COLOR0;
    nointerpolation float4 shaftColor : COLOR1;
    nointerpolation float brightness : BRIGHTNESS;
};

[shader("vertex")]
ArrowOutput arrowVertexMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const Arrows stretch = arrows[instance];
    const float3 start = stretch.start.xyz;
    const float3 end = stretch.endBrightness.xyz;
    const float2 corner = kBandCorners[vertex];
    const float3 point = lerp(start, end, corner.x);
    // Across the stretch and the line of sight, as the original turns a beam to the view; up, for a
    // stretch seen end on.
    const float3 sideways = cross(end - start, frame.eye.xyz - point);
    const float sidewaysLength = length(sideways);
    const float3 across = sidewaysLength > 0.0 ? sideways / sidewaysLength : float3(0.0, 0.0, 1.0);
    const float halfWidth = frame.arrowStyle.x * 0.5;
    ArrowOutput output;
    output.position = clipOf(point + (across * (corner.y * halfWidth)));
    output.band = float2((1.0 - corner.x) * length(end - start), corner.y * halfWidth);
    output.headColor = stretch.headColor;
    output.shaftColor = stretch.shaftColor;
    output.brightness = stretch.endBrightness.w;
    return output;
}

// How much of a pixel `inside`, a distance into a shape (outside below 0), covers, `pixel` the units a
// pixel spans.
float coverageOf(float inside, float pixel)
{
    return saturate((inside / pixel) + 0.5);
}

[shader("fragment")]
float4 arrowFragmentMain(ArrowOutput input) : SV_Target
{
    const float halfWidth = frame.arrowStyle.x * 0.5;
    const float spacing = frame.arrowStyle.y;
    // Where in its arrow's spacing the fragment is, back from the spacing's front, and how far across.
    const float back = input.band.x - (spacing * floor(input.band.x / spacing));
    const float across = abs(input.band.y);
    const float pixel = max(max(fwidth(input.band.x), fwidth(input.band.y)), 1.0e-4);

    // The head: from its tip, widening to its back.
    const float tip = kTipAt * spacing;
    const float headBack = kHeadBackAt * spacing;
    const float headWidening = (kHeadHalfWidth * halfWidth) / (headBack - tip);
    const float headSide = ((back - tip) * headWidening) - across;
    const float head = min(coverageOf(headSide * rsqrt(1.0 + (headWidening * headWidening)), pixel),
                           coverageOf(headBack - back, pixel));
    // The shaft: as wide all along, from the head's back to its tail.
    const float tail = kTailAt * spacing;
    const float shaft = min(min(coverageOf((kShaftHalfWidth * halfWidth) - across, pixel),
                                coverageOf(back - headBack, pixel)),
                            coverageOf(tail - back, pixel));
    if (head <= 0.0 && shaft <= 0.0)
    {
        discard;
    }
    const float headLight = lerp(kTipLight, kHeadBackLight, saturate((back - tip) / (headBack - tip)));
    const float shaftLight = lerp(kShaftLight, kTailLight, saturate((back - headBack) / (tail - headBack)));
    const float3 light = (lightOf(input.headColor.rgb, input.brightness) * (head * headLight)) +
                         (lightOf(input.shaftColor.rgb, input.brightness) * (shaft * shaftLight));
    return float4(light, 1.0);
}
