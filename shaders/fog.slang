// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Fog (nx.render:fog): how much of a fragment's color a fog hides, by how far the fragment is from
// the eye, and the color it then shows, as the original's OpenGL fog mixes it (GL_LINEAR, GL_EXP,
// GL_EXP2), with the nearest distance it hides anything at, the farthest it thickens to and the most of
// a color it hides. Each renderer of the scene carries a Fog in its frame (nx::render::FogBlock): the
// world (world.slang), studio models (studio.slang), sprites (sprite.slang) and particles
// (particle.slang).
module fog;

// Fog::shape's x: nx::render::FogCurve.
static const uint kLinearCurve = 0;
static const uint kExponentialCurve = 1;
static const uint kExponentialSquaredCurve = 2;
// Fog::eye's w when a fragment is as far as it is from the eye, rather than along the view.
static const float kFromTheEye = 1.0;

public struct Fog
{
    // rgb the fog's color as the target takes it; a the most of a color it hides, 0 for no fog.
    public float4 color;
    // x its curve, y its density, z its start (the nearest it hides anything), w its end (the farthest it
    // thickens to).
    public float4 shape;
    // xyz the eye; w kFromTheEye, or 0 along the view.
    public float4 eye;
    // xyz the view's forward.
    public float4 forward;
};

// How far `position`, in the world, is for `fog`: from its eye, or along its view.
float distanceOf(Fog fog, float3 position)
{
    const float3 fromEye = position - fog.eye.xyz;
    return fog.eye.w == kFromTheEye ? length(fromEye) : dot(fromEye, fog.forward.xyz);
}

// How much of a color at `position`, in the world, `fog` hides, from 0 to its opacity: its curve at the
// distance past its start, held from its end on (nx::render::fogAmountAt).
public float fogAmountOf(Fog fog, float3 position)
{
    if (fog.color.a <= 0.0)
    {
        return 0.0;
    }
    const float past = max(min(distanceOf(fog, position), fog.shape.w) - fog.shape.z, 0.0);
    const uint curve = uint(fog.shape.x);
    float thickness = 0.0;
    if (curve == kLinearCurve)
    {
        thickness = past / (fog.shape.w - fog.shape.z);
    }
    else if (curve == kExponentialCurve)
    {
        thickness = 1.0 - exp(-fog.shape.y * past);
    }
    else if (curve == kExponentialSquaredCurve)
    {
        const float scaled = fog.shape.y * past;
        thickness = 1.0 - exp(-(scaled * scaled));
    }
    return saturate(thickness) * fog.color.a;
}

// `color`, at `position`, as `fog` shows it: toward the fog's color by how much it hides there.
public float3 foggedOf(float3 color, Fog fog, float3 position)
{
    return lerp(color, fog.color.rgb, fogAmountOf(fog, position));
}
