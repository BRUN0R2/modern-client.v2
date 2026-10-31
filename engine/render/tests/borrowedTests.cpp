// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Borrowed render data accepts lvalues, but cannot be built from models or views that immediately expire.
import std;
import nx.assets;
import nx.render;

namespace {

template <class T>
concept SpriteBorrows = requires(T&& model) { nx::render::SpriteInstance{.model = std::forward<T>(model)}; };

template <class T>
concept StudioBorrows = requires(T&& model) { nx::render::StudioInstance{.model = std::forward<T>(model)}; };

template <class T>
concept WorldBorrows = requires(T&& map) { nx::render::BspWorldDesc{.map = std::forward<T>(map)}; };

template <class T>
concept StudioDescBorrowsModel = requires(T&& model, const nx::assets::MdlView& textures) {
    nx::render::StudioModelDesc{.model = std::forward<T>(model), .textures = textures};
};

template <class T>
concept StudioDescBorrowsTextures = requires(const nx::assets::MdlView& model, T&& textures) {
    nx::render::StudioModelDesc{.model = model, .textures = std::forward<T>(textures)};
};

using nx::assets::BspView;
using nx::assets::MdlView;
using nx::render::SpriteModel;
using nx::render::StudioModel;

static_assert(SpriteBorrows<SpriteModel&> && SpriteBorrows<const SpriteModel&>);
static_assert(!SpriteBorrows<SpriteModel> && !SpriteBorrows<const SpriteModel>);
static_assert(StudioBorrows<StudioModel&> && StudioBorrows<const StudioModel&>);
static_assert(!StudioBorrows<StudioModel> && !StudioBorrows<const StudioModel>);
static_assert(WorldBorrows<BspView&> && WorldBorrows<const BspView&>);
static_assert(!WorldBorrows<BspView> && !WorldBorrows<const BspView>);
static_assert(StudioDescBorrowsModel<MdlView&> && StudioDescBorrowsModel<const MdlView&>);
static_assert(!StudioDescBorrowsModel<MdlView> && !StudioDescBorrowsModel<const MdlView>);
static_assert(StudioDescBorrowsTextures<MdlView&> && StudioDescBorrowsTextures<const MdlView&>);
static_assert(!StudioDescBorrowsTextures<MdlView> && !StudioDescBorrowsTextures<const MdlView>);

} // namespace
