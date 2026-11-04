// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// What the GPU does for a crowd of models each frame (nx.render:studio, docs/adr/0032-gpu-crowds.md): each body
// along its last move, kept only if the frame may show it (the view's planes, and the leaves
// the view's leaf may see, down the map's tree, as BspWorld::mayShow), and its bones posed as cs::poseBones poses
// them: its frame estimated (R_StudioEstimateFrame), each bone between two frames of its sequence
// (R_StudioCalcBoneQuaternion, QuaternionSlerp), placed in its parent. Then how many instances each model's draws
// have. The vertices then read the bones and the instances as those of any other model.

import studioInstance;

static const uint kFrustumPlanes = 4;

struct Frame
{
    // xyz: its normal, w: its distance; what is behind any of them does not show.
    float4 planes[kFrustumPlanes];
    // How far the moment shown is from a tick to the next, from 0 to 1.
    float between;
    // How long the moment shown is past the last tick: none or less.
    float sinceLastTick;
    uint bodyCount;
    uint modelCount;
    uint drawCount;
    uint leafCount;
    uint rootNode;
    // 0: it has no map's tree, and only the view's planes hide a body.
    uint hasTree;
};

// A body as its last move left it.
struct Body
{
    // Where its last move started.
    float3 previousOrigin;
    // About the height, in radians: a body stands upright.
    float yaw;
    float3 origin;
    uint model;
    uint sequence;
    // Where its server left it, in 256ths of its sequence (pev->frame).
    float frame;
    // How long before the last tick its server left it there.
    float advancedAgo;
    float framerate;
    float3 lightDirection;
    float ambient;
    float3 lightColor;
    float shade;
    // How far from where it started it shows at the last tick, and how much farther a tick later.
    float moveShare;
    float moveSharePerTick;
};

// A model of the crowd: where its instances and bones go this frame, and where what it plays is.
struct Model
{
    uint firstInstance;
    uint firstBone;
    uint boneCount;
    uint firstSequence;
    uint firstParent;
};

// A sequence of a model: its frames' poses, how it plays, and how far about a body it may show.
struct Sequence
{
    uint firstPose;
    uint frameCount;
    float fps;
    uint isLooping;
    float reach;
    float lowest;
    float highest;
};

// A bone in a frame of a sequence, in its parent's space.
struct BonePose
{
    float4 position;
    float4 rotation;
};

// A node of the map's visible tree: a child of 0 or more is a node, less is the leaf -1 - child.
struct Node
{
    float4 plane;
    int front;
    int back;
};

// A mesh of a model, which a draw draws.
struct DrawTemplate
{
    uint indexCount;
    uint firstIndex;
    uint model;
};

// As Vulkan reads an indirect draw (rhi::IndexedDrawArguments).
struct DrawArguments
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]]
StructuredBuffer<Body> bodies;
[[vk::binding(2, 0)]]
StructuredBuffer<Model> models;
[[vk::binding(3, 0)]]
StructuredBuffer<Sequence> sequences;
[[vk::binding(4, 0)]]
StructuredBuffer<BonePose> poses;
[[vk::binding(5, 0)]]
StructuredBuffer<int> parents;
[[vk::binding(6, 0)]]
StructuredBuffer<Node> nodes;
// Bit n is leaf n + 1, as the map's compiler packs them.
[[vk::binding(7, 0)]]
StructuredBuffer<uint> visibleLeaves;
[[vk::binding(8, 0)]]
StructuredBuffer<DrawTemplate> templates;
// Three rows a bone, as shaders/studio.slang reads them.
[[vk::binding(9, 0)]]
RWStructuredBuffer<float4> bones;
[[vk::binding(10, 0)]]
RWStructuredBuffer<Instance> instances;
// How many of each model's bodies the frame shows.
[[vk::binding(11, 0)]]
RWStructuredBuffer<uint> counts;
[[vk::binding(12, 0)]]
RWStructuredBuffer<DrawArguments> draws;

static const uint kBoneRows = 3;
// What an entity's frame counts its sequence in (cs::kFrameScale).
static const float kFrameScale = 256.0;
// How far short of its last frame a sequence that does not loop stops (cs's kShortOfTheLastFrame).
static const float kShortOfTheLastFrame = 1.001;
// Beyond this, the spherical interpolation of two rotations divides by too small a sine: the original
// interpolates their quaternions linearly instead (cs's kNearlyParallel).
static const float kNearlyParallel = 0.000001;
// How deep down the map's tree a look keeps what it has yet to look at. A tree deeper than that shows the body:
// better than one wrongly hidden.
static const uint kMostLooks = 64;

bool isInFrustum(float3 lowest, float3 highest)
{
    for (uint index = 0; index < kFrustumPlanes; ++index)
    {
        const float4 plane = frame.planes[index];
        const float3 farthest = float3(plane.x >= 0.0 ? highest.x : lowest.x, plane.y >= 0.0 ? highest.y : lowest.y,
                                       plane.z >= 0.0 ? highest.z : lowest.z);
        if (dot(plane.xyz, farthest) - plane.w < 0.0)
        {
            return false;
        }
    }
    return true;
}

bool isLeafVisible(uint leaf)
{
    if (leaf == 0 || leaf > frame.leafCount)
    {
        return false;
    }
    const uint bit = leaf - 1;
    return ((visibleLeaves[bit / 32] >> (bit % 32)) & 1) != 0;
}

// BspView::touchesAnyOf: each side of a node a part of the box is on.
bool touchesVisibleLeaf(float3 lowest, float3 highest)
{
    if (frame.hasTree == 0)
    {
        return true;
    }
    int looks[kMostLooks];
    uint look = 0;
    looks[look++] = int(frame.rootNode);
    while (look > 0)
    {
        const int child = looks[--look];
        if (child < 0)
        {
            if (isLeafVisible(uint(-1 - child)))
            {
                return true;
            }
            continue;
        }
        const Node node = nodes[child];
        const float3 normal = node.plane.xyz;
        const float3 farthest =
            float3(normal.x >= 0.0 ? highest.x : lowest.x, normal.y >= 0.0 ? highest.y : lowest.y,
                   normal.z >= 0.0 ? highest.z : lowest.z);
        const float3 nearest =
            float3(normal.x >= 0.0 ? lowest.x : highest.x, normal.y >= 0.0 ? lowest.y : highest.y,
                   normal.z >= 0.0 ? lowest.z : highest.z);
        const bool isFront = dot(normal, nearest) - node.plane.w >= 0.0;
        const bool isBack = !isFront && dot(normal, farthest) - node.plane.w < 0.0;
        if (look + 2 > kMostLooks)
        {
            return true;
        }
        if (!isFront)
        {
            looks[look++] = node.back;
        }
        if (!isBack)
        {
            looks[look++] = node.front;
        }
    }
    return false;
}

// cs::estimatedFrameOf.
float estimatedFrameOf(Sequence sequence, Body body)
{
    if (sequence.frameCount <= 1)
    {
        return 0.0;
    }
    const float last = float(sequence.frameCount - 1);
    float estimated = (body.frame * last / kFrameScale) +
                      (max(body.advancedAgo + frame.sinceLastTick, 0.0) * body.framerate * sequence.fps);
    if (sequence.isLooping != 0)
    {
        estimated -= trunc(estimated / last) * last;
        if (estimated < 0.0)
        {
            estimated += last;
        }
        return estimated;
    }
    return clamp(estimated, 0.0, float(sequence.frameCount) - kShortOfTheLastFrame);
}

// cs's slerpAsTheOriginal.
float4 slerpAsTheOriginal(float4 p, float4 q, float t)
{
    const float4 apart = p - q;
    const float4 together = p + q;
    if (dot(apart, apart) > dot(together, together))
    {
        q = -q;
    }
    const float cosine = dot(p, q);
    float weightP = 1.0 - t;
    float weightQ = t;
    if (1.0 - cosine > kNearlyParallel)
    {
        const float angle = acos(cosine);
        const float sine = sin(angle);
        weightP = sin((1.0 - t) * angle) / sine;
        weightQ = sin(t * angle) / sine;
    }
    return (weightP * p) + (weightQ * q);
}

[shader("compute")]
[numthreads(16, 1, 1)]
void resetMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x < frame.modelCount)
    {
        counts[id.x] = 0;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void poseMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= frame.bodyCount)
    {
        return;
    }
    const Body body = bodies[id.x];
    const Model model = models[body.model];
    const Sequence sequence = sequences[model.firstSequence + body.sequence];
    const float3 origin =
        lerp(body.previousOrigin, body.origin, saturate(body.moveShare + (body.moveSharePerTick * frame.between)));
    // cs::shownBoxOf.
    const float3 lowest = origin + float3(-sequence.reach, -sequence.reach, sequence.lowest);
    const float3 highest = origin + float3(sequence.reach, sequence.reach, sequence.highest);
    if (!isInFrustum(lowest, highest) || !touchesVisibleLeaf(lowest, highest))
    {
        return;
    }
    uint place;
    InterlockedAdd(counts[body.model], 1, place);
    const uint firstBone = model.firstBone + (place * model.boneCount);
    const uint instance = model.firstInstance + place;
    instances[instance].lightDirection = float4(body.lightDirection, 0.0);
    instances[instance].lightColor = float4(body.lightColor, 0.0);
    instances[instance].ambient = body.ambient;
    instances[instance].shade = body.shade;
    instances[instance].depthScale = 1.0;
    instances[instance].depthOffset = 0.0;
    instances[instance].firstBone = firstBone;
    instances[instance].blend = 1.0;
    instances[instance].isUnlit = 0;
    instances[instance].lightCount = 0;
    instances[instance].probe = -1;
    instances[instance].secondProbe = -1;
    instances[instance].secondProbeShare = 0.0;

    const float estimated = estimatedFrameOf(sequence, body);
    const uint whole = uint(estimated);
    const float fraction = estimated - float(whole);
    const uint next = min(whole + 1, sequence.frameCount - 1);
    const uint firstOfFrame = sequence.firstPose + (whole * model.boneCount);
    const uint firstOfNext = sequence.firstPose + (next * model.boneCount);
    const float yawCosine = cos(body.yaw);
    const float yawSine = sin(body.yaw);
    // Where the root bones are placed in (nx::affine of nx::rotationMatrix, turned about its height).
    const float4 placement[kBoneRows] = {float4(yawCosine, -yawSine, 0.0, origin.x),
                                         float4(yawSine, yawCosine, 0.0, origin.y), float4(0.0, 0.0, 1.0, origin.z)};
    for (uint bone = 0; bone < model.boneCount; ++bone)
    {
        const BonePose from = poses[firstOfFrame + bone];
        const BonePose to = poses[firstOfNext + bone];
        const float3 position = lerp(from.position.xyz, to.position.xyz, fraction);
        const float4 q = slerpAsTheOriginal(from.rotation, to.rotation, fraction);
        // The original's QuaternionMatrix.
        const float3 row0 = float3(1.0 - (2.0 * q.y * q.y) - (2.0 * q.z * q.z), (2.0 * q.x * q.y) - (2.0 * q.w * q.z),
                                   (2.0 * q.x * q.z) + (2.0 * q.w * q.y));
        const float3 row1 = float3((2.0 * q.x * q.y) + (2.0 * q.w * q.z), 1.0 - (2.0 * q.x * q.x) - (2.0 * q.z * q.z),
                                   (2.0 * q.y * q.z) - (2.0 * q.w * q.x));
        const float3 row2 = float3((2.0 * q.x * q.z) - (2.0 * q.w * q.y), (2.0 * q.y * q.z) + (2.0 * q.w * q.x),
                                   1.0 - (2.0 * q.x * q.x) - (2.0 * q.y * q.y));
        const int parent = parents[model.firstParent + bone];
        for (uint row = 0; row < kBoneRows; ++row)
        {
            const float4 of = parent >= 0 ? bones[((firstBone + uint(parent)) * kBoneRows) + row] : placement[row];
            bones[((firstBone + bone) * kBoneRows) + row] =
                float4((of.x * row0.x) + (of.y * row1.x) + (of.z * row2.x),
                       (of.x * row0.y) + (of.y * row1.y) + (of.z * row2.y),
                       (of.x * row0.z) + (of.y * row1.z) + (of.z * row2.z),
                       (of.x * position.x) + (of.y * position.y) + (of.z * position.z) + of.w);
        }
    }
}

[shader("compute")]
[numthreads(16, 1, 1)]
void drawsMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= frame.drawCount)
    {
        return;
    }
    const DrawTemplate drawn = templates[id.x];
    draws[id.x].indexCount = drawn.indexCount;
    draws[id.x].instanceCount = counts[drawn.model];
    draws[id.x].firstIndex = drawn.firstIndex;
    draws[id.x].vertexOffset = 0;
    draws[id.x].firstInstance = models[drawn.model].firstInstance;
}
