// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A model drawn, as shaders/studio.slang reads it and shaders/crowd.slang writes it.

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
