// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Models of generic entities, drawn from the catalog their bodies use (ADR-0028).
export module cs.client:entityModels;
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;
import :posedModels;

export namespace cs {
/// Why the entities' models could not be loaded.
enum class EntityModelError : std::uint8_t {
    MaterialsInvalid, ///< A model's materials could not be read.
    UploadFailed,     ///< A model could not be uploaded to the GPU.
};

/// The GPU models of an EntityCatalog's types, in the catalog's order.
class EntityModels {
public:
    /// Appends GPU models once, without replacing models in-flight frames can still use. Record outside
    /// a render pass; catalog models must include textures. Errors: MaterialsInvalid, UploadFailed.
    [[nodiscard]] auto load(const EntityCatalog& catalog, nx::render::StudioRenderer& renderer,
                            const nx::assets::Vfs& vfs, nx::rhi::CommandList& commands)
        -> nx::Result<void, EntityModelError>;
    /// Writes to `posed` those of `entities` `frame` may show. Requires uploaded models and entity
    /// indices/sequences issued by this catalog.
    void pose(const EntityCatalog& catalog, std::span<const ShownEntity> entities, const ModelFrame& frame,
              PosedModels& posed);
    /// Writes to `room` the bodies of `crowd` alive, for the GPU to pose and draw
    /// (nx::render::StudioRenderer:: recordCrowd), in the jobs of `frame`: where each is, and its light,
    /// found anew at a tick of its own a tenth of a second apart, as often as the light styles change.
    void writeBodies(const EntityCatalog& catalog, const ShownCrowd& crowd, const ModelFrame& frame,
                     nx::render::CrowdRoom room);

private:
    /// A body the frame may show, what it is drawn with, and where its bones are among them all.
    struct Posing {
        const ShownEntity* entity = nullptr;
        const nx::render::StudioModel* drawn = nullptr;
        const AnimatedModel* file = nullptr;
        nx::Vec3 origin{};
        std::size_t firstBone = 0;
        nx::render::ModelLight light{};
    };

    /// The light a body was last found in, by the index of its entity.
    struct LitBody {
        nx::ecs::Entity entity{};
        nx::render::ModelLight light{};
    };

    std::deque<nx::render::StudioModel> models; ///< Which stay where they are: a renderer's crowd names them.
    std::vector<Posing> posings;                ///< Of a frame; kept for its room.
    std::vector<LitBody> litBodies;
};
} // namespace cs
