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

/// A map of one 64-unit square face on the plane z = 0, textured and lit: what each test breaks.
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
