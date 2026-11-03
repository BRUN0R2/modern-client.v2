// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A dispatch for the tests of nx.rhi's compute pipelines: twice each of the values it is given, and how many a
// draw of them would take.

[[vk::binding(0, 0)]]
StructuredBuffer<uint> values;
[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> doubled;

[shader("compute")]
[numthreads(4, 1, 1)]
void doubleMain(uint3 id : SV_DispatchThreadID)
{
    uint count;
    uint stride;
    values.GetDimensions(count, stride);
    if (id.x < count)
    {
        doubled[id.x] = values[id.x] * 2;
    }
}
