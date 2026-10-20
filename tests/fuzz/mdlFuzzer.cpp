// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// MDL v10 studio models (nx.assets:mdl). A model that parses is posed and drawn as a renderer
// would, without checks: every bone's parent comes before it, every index is in range, and every
// skin reference of a model with textures names one of its textures. Its animations decode, or fail
// with a typed error.

import std;
import nx.math;
import nx.assets;

#include "fuzzTarget.hpp"

void nx::test::fuzz(std::span<const std::byte> input) {
    const auto model = nx::assets::MdlView::parse(input);
    if (!model) {
        return;
    }
    // The skeleton in one pass, parents first.
    std::vector<float> heights(model->bones().size());
    for (std::size_t bone = 0; bone < model->bones().size(); ++bone) {
        const auto& parent = model->bones()[bone].parent;
        heights[bone] = (parent ? heights[parent->value] : 0.0F) +
                        model->bones()[bone].values[std::to_underlying(nx::assets::BoneAxis::Z)];
    }
    float sum = 0.0F;
    for (const nx::assets::BoneController& controller : model->boneControllers()) {
        sum += heights[controller.bone.value];
    }
    for (const nx::assets::Hitbox& hitbox : model->hitboxes()) {
        sum += heights[hitbox.bone.value];
    }
    for (const nx::assets::Attachment& attachment : model->attachments()) {
        sum += heights[attachment.bone.value];
    }
    for (const nx::assets::BodyPart& part : model->bodyParts()) {
        for (const nx::assets::StudioModel& choice : part.models) {
            for (const nx::assets::StudioMesh& mesh : choice.meshes) {
                if (!model->skins().empty()) {
                    const auto texture = model->skins()[0][mesh.skinReference];
                    sum += static_cast<float>(model->textures()[texture.value].width);
                }
                for (const nx::assets::TriangleRun& run : mesh.runs) {
                    for (const nx::assets::MeshCorner& corner : run.corners) {
                        const auto& vertex = choice.vertices[corner.vertex.value];
                        sum += vertex.vector.x + heights[vertex.bone.value];
                        sum += choice.normals[corner.normal.value].vector.z;
                    }
                }
            }
        }
    }
    // Every animation in this file, read back through value() as a pose would.
    for (std::uint32_t sequence = 0; sequence < model->sequences().size(); ++sequence) {
        if (model->sequences()[sequence].group.value != 0) {
            continue; // in another file
        }
        const auto frames = model->decodeAnimation(nx::assets::SequenceIndex{.value = sequence}, input);
        if (!frames) {
            continue;
        }
        for (std::uint32_t bone = 0; bone < frames->boneCount; ++bone) {
            sum += static_cast<float>(frames->value(frames->blendCount - 1, frames->frameCount - 1,
                                                    nx::assets::BoneIndex{.value = bone},
                                                    nx::assets::BoneAxis::RotationZ)) *
                   model->bones()[bone].scales.back();
        }
    }
    std::ignore = sum; // the reads are the test
}
