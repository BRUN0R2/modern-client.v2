// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How the colors a renderer draws reach its target (nx.render's ColorEncoding), which each renderer's
// frame carries, and the display the original's colors are made for: the world (world.slang), studio
// models (studio.slang), sprites (sprite.slang) and the enhanced look's last pass (post.slang).
module colorEncoding;

// ColorEncoding::Display: the original's colors as its screen shows them, blended as they are.
public static const uint kDisplayEncoding = 0;
// ColorEncoding::Linear: linear light, color textures sampled from sRGB, which the post pass encodes
// for the screen once.
public static const uint kLinearEncoding = 1;

// The gamma of the display the original's colors, its palettes and its light, are made for.
public static const float kDisplayGamma = 2.2;

// A display color as linear light.
public float3 linearOf(float3 display)
{
    return pow(max(display, 0.0), kDisplayGamma);
}

// Linear light as a display color.
public float3 displayOf(float3 linear)
{
    return pow(max(linear, 0.0), 1.0 / kDisplayGamma);
}

// A display color as a target of `encoding` takes it: as it is, or as linear light.
public float3 encodedOf(float3 display, uint encoding)
{
    return encoding == kLinearEncoding ? linearOf(display) : display;
}
