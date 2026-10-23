// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Where a direction reaches the sky, as the original projects it (vec_to_st): the sky the world draws
// (world.slang), and the one the enhanced look's models reflect (studio.slang).
module skyDirection;

/// Where a direction reaches the sky: a side, and a point of it.
public struct SkyPoint
{
    public float side; // the layer of the sky's sides, toward +X, -X, +Y, -Y, +Z and -Z
    public float2 st;  // s to the right and t up, from -1 to 1
}

// The layers of the sky's sides, by the axis their side faces.
static const float kSkyPositiveX = 0.0;
static const float kSkyNegativeX = 1.0;
static const float kSkyPositiveY = 2.0;
static const float kSkyNegativeY = 3.0;
static const float kSkyPositiveZ = 4.0;
static const float kSkyNegativeZ = 5.0;
// The least a direction's largest axis is divided by: a direction of zero reaches an edge of a side.
static const float kLeastAxis = 1e-30;

/// The side `direction` points at, by its largest axis with ties broken as the original breaks them,
/// and the point it reaches there, as the original projects it (vec_to_st): from the middle of the
/// sky, each side is seen upright and unmirrored, the top and bottom ones facing +X.
public SkyPoint skyPointOf(float3 direction)
{
    const float3 size = abs(direction);
    SkyPoint point;
    if (size.x > size.y && size.x > size.z)
    {
        point.side = direction.x > 0.0 ? kSkyPositiveX : kSkyNegativeX;
        point.st = float2(-direction.y * sign(direction.x), direction.z) / size.x;
    }
    else if (size.y > size.z && size.y > size.x)
    {
        point.side = direction.y > 0.0 ? kSkyPositiveY : kSkyNegativeY;
        point.st = float2(direction.x * sign(direction.y), direction.z) / size.y;
    }
    else
    {
        // A tie of x and y with no z ends here, as in the original: the edge of the side, not 0 / 0.
        point.side = direction.z > 0.0 ? kSkyPositiveZ : kSkyNegativeZ;
        point.st = float2(-direction.y, -direction.x * sign(direction.z)) / max(size.z, kLeastAxis);
    }
    return point;
}

/// The texture coordinates of `point` on its side: s from the left, t from the top, from 0 to 1.
public float2 skyUvOf(SkyPoint point)
{
    return float2(point.st.x + 1.0, 1.0 - point.st.y) * 0.5; // texture v is down
}
