// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The RHI's tests: a triangle from a vertex buffer, in the color of a uniform buffer.

struct Fill
{
    float4 color;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Fill> fill;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
};

[shader("vertex")]
float4 vertexMain(VertexInput input) : SV_Position
{
    return float4(input.position, 0.0, 1.0);
}

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return fill.color;
}

// Dual-source blending: nothing added, and what is under the fragment kept by the uniform's color,
// channel by channel, the fragment's second color.
struct KeepingOutput
{
    [[vk::location(0), vk::index(0)]] float4 added : SV_Target0;
    [[vk::location(0), vk::index(1)]] float4 kept : SV_Target1;
};

[shader("fragment")]
KeepingOutput keepingFragmentMain()
{
    KeepingOutput output;
    output.added = float4(0.0, 0.0, 0.0, 0.0);
    output.kept = fill.color;
    return output;
}
