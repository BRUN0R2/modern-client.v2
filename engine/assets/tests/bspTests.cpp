// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace {

using nx::assets::BspError;
using nx::assets::BspView;
using nx::assets::ClipNodeChild;
using nx::assets::ClipNodeIndex;
using nx::assets::Contents;
using nx::assets::LeafIndex;
using nx::assets::NodeChild;
using nx::assets::PlaneAxis;

using Bytes = std::vector<std::byte>;

// The lumps of a BSP v30 file, in header order.
enum class Lump : std::uint8_t {
    Entities,
    Planes,
    Textures,
    Vertices,
    Visibility,
    Nodes,
    TextureInfos,
    Faces,
    Lighting,
    ClipNodes,
    Leaves,
    MarkSurfaces,
    Edges,
    SurfaceEdges,
    Models,
    Count,
};

template <class Value>
void append(Bytes& bytes, Value value) {
    const auto raw =
        std::bit_cast<std::array<std::byte, sizeof(Value)>>(value); // the tests run on little-endian hosts
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

void appendVec3(Bytes& bytes, nx::Vec3 vector) {
    append(bytes, vector.x);
    append(bytes, vector.y);
    append(bytes, vector.z);
}

/// A mip texture of `size` by `size`; with no pixels when they are in a WAD.
[[nodiscard]] auto mipTexture(std::string_view name, std::uint32_t size, bool hasPixels) -> Bytes {
    Bytes lump;
    for (std::size_t index = 0; index < 16; ++index) {
        lump.push_back(index < name.size() ? static_cast<std::byte>(name[index]) : std::byte{0});
    }
    append(lump, size);
    append(lump, size);
    std::uint32_t offset = 40;
    for (std::uint32_t level = 0; level < 4; ++level) {
        append(lump, hasPixels ? offset : 0U);
        offset += (size >> level) * (size >> level);
    }
    if (hasPixels) {
        lump.resize(offset, std::byte{1});
        append(lump, std::uint16_t{256});
        lump.resize(lump.size() + 768, std::byte{2});
    }
    return lump;
}

void appendShortBounds(Bytes& bytes, std::array<std::int16_t, 6> bounds) {
    for (const std::int16_t bound : bounds) {
        append(bytes, bound);
    }
}

/// A map of one 64-unit square face on the plane z = 0, textured and lit, with its tree: a node
/// whose front is the empty leaf 1 (the face) and whose back is the solid leaf 0, the same split as
/// a collision hull, the world model and its entity. What each test breaks.
struct QuadMap {
    nx::EnumArray<Lump, Bytes> lumps;

    QuadMap() {
        Bytes& planes = lumps[Lump::Planes];
        appendVec3(planes, {.x = 0.0F, .y = 0.0F, .z = 1.0F});
        append(planes, 0.0F);
        append(planes, std::int32_t{2});

        Bytes& vertices = lumps[Lump::Vertices];
        for (const auto [x, y] : std::to_array<std::pair<float, float>>(
                 {{0.0F, 0.0F}, {64.0F, 0.0F}, {64.0F, 64.0F}, {0.0F, 64.0F}})) {
            appendVec3(vertices, {.x = x, .y = y, .z = 0.0F});
        }

        Bytes& edges = lumps[Lump::Edges]; // edge 0 is unused, as compilers leave it
        for (const auto [from, to] :
             std::to_array<std::array<std::uint16_t, 2>>({{0, 0}, {0, 1}, {1, 2}, {2, 3}, {3, 0}})) {
            append(edges, from);
            append(edges, to);
        }

        Bytes& surfaceEdges = lumps[Lump::SurfaceEdges];
        for (const std::int32_t edge : {1, 2, 3, -4}) {
            append(surfaceEdges, edge);
        }

        const Bytes texture = mipTexture("CRATE01", 16, true);
        Bytes& textures = lumps[Lump::Textures];
        append(textures, std::int32_t{2});
        append(textures, std::int32_t{12});
        append(textures, std::int32_t{-1}); // an empty slot
        textures.insert(textures.end(), texture.begin(), texture.end());

        Bytes& textureInfos = lumps[Lump::TextureInfos];
        for (const float value : {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F}) {
            append(textureInfos, value);
        }
        append(textureInfos, std::int32_t{0});
        append(textureInfos, std::int32_t{0});

        lumps[Lump::Lighting] = Bytes(5 * 5 * 3, std::byte{200});

        Bytes& faces = lumps[Lump::Faces];
        append(faces, std::uint16_t{0}); // plane
        append(faces, std::int16_t{0});  // side
        append(faces, std::int32_t{0});  // first surface edge
        append(faces, std::int16_t{4});  // edge count
        append(faces, std::int16_t{0});  // texture info
        for (const std::uint8_t style : std::to_array<std::uint8_t>({0, 255, 255, 255})) {
            faces.push_back(static_cast<std::byte>(style));
        }
        append(faces, std::int32_t{0}); // lightmap offset

        append(lumps[Lump::MarkSurfaces], std::uint16_t{0});
        lumps[Lump::Visibility] = Bytes{std::byte{0xFF}};

        Bytes& nodes = lumps[Lump::Nodes];
        append(nodes, std::int32_t{0});  // plane
        append(nodes, std::int16_t{-2}); // front: leaf 1
        append(nodes, std::int16_t{-1}); // back: leaf 0
        appendShortBounds(nodes, {0, 0, -1, 64, 64, 1});
        append(nodes, std::uint16_t{0}); // first face
        append(nodes, std::uint16_t{1}); // face count

        Bytes& leaves = lumps[Lump::Leaves];
        for (const auto [contents, visibility, markSurfaces] :
             std::to_array<std::array<std::int32_t, 3>>({{-2, -1, 0}, {-1, 0, 1}})) {
            append(leaves, contents);
            append(leaves, visibility);
            appendShortBounds(leaves, {0, 0, 0, 64, 64, 64});
            append(leaves, std::uint16_t{0});
            append(leaves, static_cast<std::uint16_t>(markSurfaces));
            for (const std::uint8_t level : std::to_array<std::uint8_t>({0, 10, 0, 0})) {
                leaves.push_back(static_cast<std::byte>(level));
            }
        }

        Bytes& clipNodes = lumps[Lump::ClipNodes];
        append(clipNodes, std::int32_t{0});
        append(clipNodes, std::int16_t{-1}); // front: empty
        append(clipNodes, std::int16_t{-2}); // back: solid

        Bytes& models = lumps[Lump::Models];
        appendVec3(models, {.x = 0.0F, .y = 0.0F, .z = -1.0F});
        appendVec3(models, {.x = 64.0F, .y = 64.0F, .z = 1.0F});
        appendVec3(models, {});
        for (const std::int32_t head : {0, 0, 0, -1}) { // the node, two hulls, an empty third hull
            append(models, head);
        }
        append(models, std::int32_t{1}); // visible leaves
        append(models, std::int32_t{0}); // first face
        append(models, std::int32_t{1}); // face count

        setEntities(R"({
"classname" "worldspawn"
"wad" "\half-life\valve\halflife.wad;\half-life\cstrike\cstrike.wad"
"skyname" "desert"
}
)");
    }

    void setEntities(std::string_view entities) {
        lumps[Lump::Entities] = entities |
                                std::views::transform([](char c) { return static_cast<std::byte>(c); }) |
                                std::ranges::to<Bytes>();
        lumps[Lump::Entities].push_back(std::byte{0}); // compilers end the text with a zero byte
    }

    [[nodiscard]] auto file() const -> Bytes {
        Bytes bytes;
        append(bytes, std::int32_t{30});
        std::uint32_t offset = 4 + (static_cast<std::uint32_t>(lumps.size()) * 8);
        for (const Bytes& lump : lumps) {
            append(bytes, offset);
            append(bytes, static_cast<std::uint32_t>(lump.size()));
            offset += static_cast<std::uint32_t>(lump.size());
        }
        for (const Bytes& lump : lumps) {
            bytes.insert(bytes.end(), lump.begin(), lump.end());
        }
        return bytes;
    }

    /// Overwrites the bytes of `lump` at `offset` with `value`.
    template <class Value>
    void set(Lump lump, std::size_t offset, Value value) {
        Bytes encoded;
        append(encoded, value);
        std::ranges::copy(encoded, lumps[lump].begin() + static_cast<std::ptrdiff_t>(offset));
    }
};

[[nodiscard]] auto parseError(const QuadMap& map) -> BspError {
    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    EXPECT_FALSE(view.has_value());
    return view ? BspError{} : view.error().code;
}

TEST(BspTest, ReadsTheGeometryOfAMap) {
    const QuadMap map;
    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());

    ASSERT_EQ(view->planes().size(), 1U);
    EXPECT_EQ(view->planes()[0].plane,
              (nx::Plane{.normal = {.x = 0.0F, .y = 0.0F, .z = 1.0F}, .distance = 0.0F}));
    EXPECT_EQ(view->planes()[0].axis, PlaneAxis::Z);
    EXPECT_EQ(view->vertices().size(), 4U);
    EXPECT_EQ(view->vertices()[2], (nx::Vec3{.x = 64.0F, .y = 64.0F, .z = 0.0F}));
    EXPECT_EQ(view->edges().size(), 5U);
    ASSERT_EQ(view->surfaceEdges().size(), 4U);
    EXPECT_EQ(view->surfaceEdges()[3].edge.value, 4U);
    EXPECT_TRUE(view->surfaceEdges()[3].isReversed);

    ASSERT_EQ(view->textures().size(), 2U);
    ASSERT_TRUE(view->textures()[0].has_value());
    EXPECT_EQ(view->textures()[0]->name, "CRATE01");
    EXPECT_TRUE(view->textures()[0]->hasPixels());
    EXPECT_FALSE(view->textures()[1].has_value()) << "the slot the compiler left empty";
    ASSERT_EQ(view->textureInfos().size(), 1U);
    EXPECT_EQ(view->textureInfos()[0].s, (nx::Vec4{.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F}));
    EXPECT_FALSE(view->textureInfos()[0].isSpecial);

    ASSERT_EQ(view->faces().size(), 1U);
    const auto& face = view->faces()[0];
    EXPECT_EQ(face.edgeCount, 4U);
    EXPECT_FALSE(face.isBackSide);
    EXPECT_EQ(face.lightStyles, (std::array<std::uint8_t, 4>{0, 255, 255, 255}));
    EXPECT_EQ(face.lightmapOffset, std::optional<std::uint32_t>{0});
    EXPECT_EQ(view->lighting().size(), 75U);
}

TEST(BspTest, ATextureMayLeaveItsPixelsToAWad) {
    QuadMap map;
    const Bytes texture = mipTexture("{BLUE", 32, false);
    Bytes& textures = map.lumps[Lump::Textures];
    textures.resize(12);
    textures.insert(textures.end(), texture.begin(), texture.end());

    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    ASSERT_TRUE(view->textures()[0].has_value());
    EXPECT_EQ(view->textures()[0]->name, "{BLUE");
    EXPECT_EQ(view->textures()[0]->width, 32U);
    EXPECT_FALSE(view->textures()[0]->hasPixels());
}

TEST(BspTest, AnUnlitFaceHasNoLightmap) {
    QuadMap map;
    map.set(Lump::Faces, 16, std::int32_t{-1});
    map.lumps[Lump::Lighting].clear();

    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    EXPECT_FALSE(view->faces()[0].lightmapOffset.has_value());
}

TEST(BspTest, ReadsTheTreeTheModelsAndTheEntities) {
    const QuadMap map;
    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());

    ASSERT_EQ(view->nodes().size(), 1U);
    EXPECT_EQ(view->nodes()[0].children[0], (NodeChild{LeafIndex{.value = 1}}));
    EXPECT_EQ(view->nodes()[0].children[1], (NodeChild{LeafIndex{.value = 0}}));
    EXPECT_EQ(view->nodes()[0].bounds.max, (nx::Vec3{.x = 64.0F, .y = 64.0F, .z = 1.0F}));
    EXPECT_EQ(view->nodes()[0].faceCount, 1U);

    ASSERT_EQ(view->leaves().size(), 2U);
    EXPECT_EQ(view->leaves()[0].contents, Contents::Solid);
    EXPECT_FALSE(view->leaves()[0].visibilityOffset.has_value());
    EXPECT_EQ(view->leaves()[1].contents, Contents::Empty);
    EXPECT_EQ(view->leaves()[1].visibilityOffset, std::optional<std::uint32_t>{0});
    EXPECT_EQ(view->leaves()[1].markSurfaceCount, 1U);
    EXPECT_EQ(view->leaves()[1].ambientLevels[1], 10) << "sky";
    ASSERT_EQ(view->markSurfaces().size(), 1U);
    EXPECT_EQ(view->markSurfaces()[0].value, 0U);
    EXPECT_EQ(view->visibility().size(), 1U);

    ASSERT_EQ(view->clipNodes().size(), 1U);
    EXPECT_EQ(view->clipNodes()[0].children[0], ClipNodeChild{Contents::Empty});
    EXPECT_EQ(view->clipNodes()[0].children[1], ClipNodeChild{Contents::Solid});

    ASSERT_EQ(view->models().size(), 1U);
    const auto& world = view->models()[0];
    EXPECT_EQ(world.headNode.value, 0U);
    EXPECT_EQ(world.hulls[0], ClipNodeChild{ClipNodeIndex{.value = 0}});
    EXPECT_EQ(world.hulls[2], ClipNodeChild{Contents::Empty});
    EXPECT_EQ(world.visibleLeafCount, 1U);
    EXPECT_EQ(world.faceCount, 1U);

    ASSERT_EQ(view->entities().size(), 1U);
    const auto& worldspawn = view->entities()[0];
    EXPECT_EQ(worldspawn.find("classname"), std::optional<std::string_view>{"worldspawn"});
    EXPECT_EQ(
        worldspawn.find("wad"),
        std::optional<std::string_view>{R"(\half-life\valve\halflife.wad;\half-life\cstrike\cstrike.wad)"})
        << "backslashes are text, not escapes";
    EXPECT_FALSE(worldspawn.find("message").has_value());
}

TEST(BspTest, HullsMayShareClipNodes) {
    QuadMap map;
    Bytes& clipNodes = map.lumps[Lump::ClipNodes];
    for (const auto [front, back] : std::to_array<std::array<std::int16_t, 2>>({{0, 0}, {0, 1}})) {
        append(clipNodes, std::int32_t{0}); // plane
        append(clipNodes, front);
        append(clipNodes, back);
    }
    map.set(Lump::Models, 44, std::int32_t{2}); // hull 2: node 2, over node 1, both over node 0

    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    EXPECT_EQ(view->clipNodes()[1].children[0], ClipNodeChild{ClipNodeIndex{.value = 0}})
        << "a child before its parent";
    EXPECT_EQ(view->models()[0].hulls[1], ClipNodeChild{ClipNodeIndex{.value = 2}});

    QuadMap cycle = map;
    cycle.set(Lump::ClipNodes, 4, std::int16_t{2}); // node 0 under node 2, which is over it
    EXPECT_EQ(parseError(cycle), BspError::BadValue);
}

TEST(BspTest, AHullHeadAtTheClipNodeCountHoldsNothing) {
    QuadMap map;
    map.set(Lump::Models, 40, std::int32_t{1}); // hull 1: what hlbsp writes for a hull without clip nodes

    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    EXPECT_EQ(view->models()[0].hulls[0], ClipNodeChild{Contents::Empty});
}

TEST(BspTest, AKeyThatRepeatsHasItsLastValue) {
    QuadMap map;
    map.setEntities(R"({ "classname" "worldspawn" "skyname" "desert" "skyname" "night" }
{ "classname" "info_player_start" "origin" "0 0 36" })");
    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    ASSERT_TRUE(view.has_value()) << nx::describe(view.error());
    ASSERT_EQ(view->entities().size(), 2U);
    EXPECT_EQ(view->entities()[0].find("skyname"), std::optional<std::string_view>{"night"});
    EXPECT_EQ(view->entities()[1].find("origin"), std::optional<std::string_view>{"0 0 36"});
}

TEST(BspTest, RejectsABrokenTree) {
    const auto withChange = [](auto change) {
        QuadMap map;
        change(map);
        return parseError(map);
    };
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Nodes, 4, std::int16_t{0}); }), BspError::BadValue)
        << "a node that is its own child would cycle";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Nodes, 4, std::int16_t{-3}); }), BspError::BadIndex)
        << "leaf 2 of 2";
    EXPECT_EQ(withChange([](QuadMap& map) {
                  // Node 0 points twice at node 1, whose sides are the two leaves: a shared subtree.
                  Bytes second(map.lumps[Lump::Nodes]);
                  map.lumps[Lump::Nodes].insert(map.lumps[Lump::Nodes].end(), second.begin(), second.end());
                  map.set(Lump::Nodes, 4, std::int16_t{1});
                  map.set(Lump::Nodes, 6, std::int16_t{1});
              }),
              BspError::BadValue)
        << "a node with two parents";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Nodes, 0, std::int32_t{1}); }), BspError::BadIndex)
        << "plane 1 of 1";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Nodes, 22, std::uint16_t{2}); }),
              BspError::BadIndex)
        << "faces 0 to 2 of 1";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Leaves, 28, std::int32_t{3}); }),
              BspError::BadValue)
        << "contents 3";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Leaves, 28 + 4, std::int32_t{1}); }),
              BspError::BadIndex)
        << "visibility past its lump";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Leaves, 28 + 22, std::uint16_t{2}); }),
              BspError::BadIndex)
        << "mark surfaces 0 to 2 of 1";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::MarkSurfaces, 0, std::uint16_t{1}); }),
              BspError::BadIndex)
        << "face 1 of 1";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::ClipNodes, 4, std::int16_t{0}); }),
              BspError::BadValue)
        << "a clip node that is its own child would cycle";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::ClipNodes, 6, std::int16_t{-16}); }),
              BspError::BadValue)
        << "contents -16";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Models, 36, std::int32_t{1}); }),
              BspError::BadIndex)
        << "head node 1 of 1";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Models, 40, std::int32_t{2}); }),
              BspError::BadIndex)
        << "hull clip node 2 of 1";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Models, 52, std::int32_t{2}); }),
              BspError::BadIndex)
        << "two visible leaves after leaf 0, of two";
    EXPECT_EQ(
        withChange([](QuadMap& map) { map.set(Lump::Models, 0, std::numeric_limits<float>::quiet_NaN()); }),
        BspError::BadValue);
}

TEST(BspTest, RejectsBrokenEntityText) {
    for (const std::string_view entities :
         {std::string_view{R"("classname" "worldspawn")"},
          std::string_view{"{\n\"classname\" \"worldspawn\"\n"}, std::string_view{"{\n\"classname\"\n}"},
          std::string_view{"{ \"classname\" \"worldspawn }"}}) {
        QuadMap map;
        map.setEntities(entities);
        const Bytes file = map.file();
        const auto view = BspView::parse(file);
        ASSERT_FALSE(view.has_value()) << entities;
        EXPECT_EQ(view.error().code, BspError::BadEntities);
        EXPECT_TRUE(view.error().detail.starts_with("Entities: line ")) << view.error().detail;
    }
}

TEST(BspTest, RejectsABrokenFile) {
    EXPECT_EQ(BspView::parse(Bytes(100)).error().code, BspError::TooSmall);

    QuadMap version29;
    Bytes file = version29.file();
    file[0] = std::byte{29};
    EXPECT_EQ(BspView::parse(file).error().code, BspError::UnsupportedVersion);

    file = QuadMap{}.file();
    file[4 + (8 * std::to_underlying(Lump::Faces)) + 5] = std::byte{0x10}; // the faces grow by 4 KiB
    EXPECT_EQ(BspView::parse(file).error().code, BspError::LumpOutOfBounds);

    QuadMap misaligned;
    misaligned.lumps[Lump::Planes].push_back(std::byte{0});
    EXPECT_EQ(parseError(misaligned), BspError::MisalignedLump);

    QuadMap darkness;
    darkness.lumps[Lump::Lighting].pop_back();
    EXPECT_EQ(parseError(darkness), BspError::MisalignedLump) << "lighting is RGB";
}

TEST(BspTest, RejectsAnIndexToNothing) {
    const auto withChange = [](auto change) {
        QuadMap map;
        change(map);
        return parseError(map);
    };
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Edges, 4 * 4 + 2, std::uint16_t{4}); }),
              BspError::BadIndex)
        << "an edge to vertex 4 of 4";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::SurfaceEdges, 0, std::int32_t{5}); }),
              BspError::BadIndex)
        << "edge 5 of 5";
    EXPECT_EQ(withChange([](QuadMap& map) {
                  map.set(Lump::SurfaceEdges, 0, std::numeric_limits<std::int32_t>::min());
              }),
              BspError::BadIndex)
        << "the value without a magnitude";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::TextureInfos, 32, std::int32_t{2}); }),
              BspError::BadIndex)
        << "texture 2 of 2";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Faces, 0, std::uint16_t{1}); }), BspError::BadIndex)
        << "plane 1 of 1";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Faces, 4, std::int32_t{1}); }), BspError::BadIndex)
        << "edges 1 to 5 of 4";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Faces, 10, std::int16_t{1}); }), BspError::BadIndex)
        << "texture info 1 of 1";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Faces, 16, std::int32_t{75}); }),
              BspError::BadIndex)
        << "a lightmap past the lighting";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Faces, 16, std::int32_t{-2}); }),
              BspError::BadIndex);
}

TEST(BspTest, RejectsAValueOutsideItsDomain) {
    const auto withChange = [](auto change) {
        QuadMap map;
        change(map);
        return parseError(map);
    };
    EXPECT_EQ(
        withChange([](QuadMap& map) { map.set(Lump::Planes, 12, std::numeric_limits<float>::quiet_NaN()); }),
        BspError::BadValue);
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Planes, 8, 2.0F); }), BspError::BadValue)
        << "a normal of length 2";
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Planes, 16, std::int32_t{6}); }),
              BspError::BadValue)
        << "plane type 6";
    EXPECT_EQ(
        withChange([](QuadMap& map) { map.set(Lump::Vertices, 0, std::numeric_limits<float>::infinity()); }),
        BspError::BadValue);
    EXPECT_EQ(withChange([](QuadMap& map) {
                  map.set(Lump::TextureInfos, 4, std::numeric_limits<float>::infinity());
              }),
              BspError::BadValue);
    EXPECT_EQ(withChange([](QuadMap& map) { map.set(Lump::Faces, 8, std::int16_t{2}); }), BspError::BadValue)
        << "a face of two edges";
}

TEST(BspTest, ABrokenEmbeddedTextureIsReported) {
    QuadMap map;
    map.set(Lump::Textures, 12 + 16, std::uint32_t{20}); // a width that is not a multiple of 16
    const Bytes file = map.file();
    const auto view = BspView::parse(file);
    ASSERT_FALSE(view.has_value());
    EXPECT_EQ(view.error().code, BspError::BadTexture);
    ASSERT_FALSE(view.error().cause.empty());
    EXPECT_EQ(view.error().cause.records()[0].code, "BadTexture");
}

} // namespace
