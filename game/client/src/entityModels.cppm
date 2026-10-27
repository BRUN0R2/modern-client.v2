// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Models of generic entities, drawn from the catalog their bodies use (ADR-0028).
export module cs.client:entityModels;
import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;
import :posedModels;

export namespace cs {
enum class EntityModelError : std::uint8_t { MaterialsInvalid, UploadFailed };
class EntityModels {
public:
    /// Appends GPU models once, without replacing models in-flight frames can still use.
    [[nodiscard]] auto load(const EntityCatalog& catalog, nx::render::StudioRenderer& renderer,
                            const nx::assets::Vfs& vfs, nx::rhi::CommandList& commands)
        -> nx::Result<void, EntityModelError>;
    [[nodiscard]] auto posed(const EntityCatalog& catalog, std::span<const ShownEntity> entities,
                             const nx::render::WorldLight& light, const nx::render::SkyLight& sky,
                             const nx::render::LightStyleValues& styles) const -> PosedModels;

private:
    std::vector<nx::render::StudioModel> models;
};
} // namespace cs
