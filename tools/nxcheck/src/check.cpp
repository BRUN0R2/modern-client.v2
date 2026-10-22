// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module tool.nxcheck;

import std;
import nx.core;
import nx.jobs;
import nx.assets;
import nx.math;
import nx.collision;
import cs.shared;

namespace nxcheck {

namespace {

using nx::fail;
using nx::Result;
using nx::assets::BspView;
using nx::assets::KeyValuesEscapes;
using nx::assets::MdlView;
using nx::assets::SpriteView;
using nx::assets::Vfs;
using nx::assets::VfsPath;
using nx::assets::WadLumpType;
using nx::assets::WadView;

/// A sequence group file starts with "IDSQ"; a model, or its `*T.mdl`, with "IDST".

/// Reads a file that `model` joins (`role` says how), mapping every failure to DependencyFailed.
[[nodiscard]] auto readJoined(const Vfs& vfs, const nx::Result<VfsPath, nx::assets::VfsError>& path,
                              std::string_view role) -> Result<std::vector<std::byte>, CheckError> {
    if (!path) {
        return fail(CheckError::DependencyFailed, std::string{role}, path.error());
    }
    auto bytes = vfs.readAll(*path);
    if (!bytes) {
        return fail(CheckError::DependencyFailed, std::format("{} {:?}", role, path->text()),
                    std::move(bytes.error()));
    }
    return std::move(*bytes);
}

[[nodiscard]] auto checkBsp(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    auto map = BspView::parse(bytes);
    if (!map) {
        return fail(CheckError::Invalid, std::move(map.error()));
    }
    if (auto hulls = nx::collision::BrushHulls::create(*map); !hulls) {
        return fail(CheckError::Invalid, "the collision hulls", std::move(hulls.error()));
    }
    return {};
}

[[nodiscard]] auto checkWad(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    auto wad = WadView::parse(bytes);
    if (!wad) {
        return fail(CheckError::Invalid, std::move(wad.error()));
    }
    for (const nx::assets::WadEntry& entry : wad->entries()) {
        Result<void, nx::assets::WadError> read{};
        switch (entry.type) {
        case WadLumpType::MipTexture:
            read = nx::assets::readTexture(entry).transform([](const auto&) {});
            break;
        case WadLumpType::Picture:
            read = nx::assets::readPicture(entry).transform([](const auto&) {});
            break;
        case WadLumpType::Font:
            read = nx::assets::readFont(entry).transform([](const auto&) {});
            break;
        default:
            continue; // a type no reader decodes
        }
        if (!read) {
            return fail(CheckError::Invalid, std::format("{} {:?}", nx::enumName(entry.type), entry.name),
                        std::move(read.error()));
        }
    }
    return {};
}

[[nodiscard]] auto checkSpriteList(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    const std::string text = bytes |
                             std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
                             std::ranges::to<std::string>();
    if (auto list = nx::assets::parseSpriteList(text); !list) {
        return fail(CheckError::Invalid, std::move(list.error()));
    }
    return {};
}

/// The KeyValues text of `bytes`, its quoted words spelled as `escapes` says.
[[nodiscard]] auto checkKeyValues(std::span<const std::byte> bytes, KeyValuesEscapes escapes)
    -> Result<void, CheckError> {
    auto text = nx::assets::keyValuesText(bytes);
    if (!text) {
        return fail(CheckError::Invalid, std::move(text.error()));
    }
    if (auto entries = nx::assets::parseKeyValues(*text, escapes); !entries) {
        return fail(CheckError::Invalid, std::move(entries.error()));
    }
    return {};
}

/// Whether `path` is a VGUI2 scheme, as VGUI2 names them (resource/ClientScheme.res).
[[nodiscard]] auto isScheme(const VfsPath& path) noexcept -> bool {
    constexpr std::string_view kSuffix = "scheme.res";
    const std::string_view text = path.text();
    return text.size() >= kSuffix.size() &&
           nx::equalsIgnoringCase(text.substr(text.size() - kSuffix.size()), kSuffix);
}

/// The scheme of `bytes`.
[[nodiscard]] auto checkScheme(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    auto text = nx::assets::keyValuesText(bytes);
    if (!text) {
        return fail(CheckError::Invalid, std::move(text.error()));
    }
    auto entries = nx::assets::parseKeyValues(*text);
    if (!entries) {
        return fail(CheckError::Invalid, std::move(entries.error()));
    }
    if (auto scheme = nx::assets::readScheme(*entries); !scheme) {
        return fail(CheckError::Invalid, std::move(scheme.error()));
    }
    return {};
}

/// The localized strings of `bytes`.
[[nodiscard]] auto checkLocalization(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    auto text = nx::assets::keyValuesText(bytes);
    if (!text) {
        return fail(CheckError::Invalid, std::move(text.error()));
    }
    auto entries = nx::assets::parseKeyValues(*text, KeyValuesEscapes::Backslash);
    if (!entries) {
        return fail(CheckError::Invalid, std::move(entries.error()));
    }
    if (auto strings = nx::assets::LocalizedStrings::read(*entries); !strings) {
        return fail(CheckError::Invalid, std::move(strings.error()));
    }
    return {};
}

/// The byte order mark of UTF-16 little-endian, which VGUI2's localized strings start with.
constexpr std::array kUtf16Mark{std::byte{0xFF}, std::byte{0xFE}};

/// How far a posed bone's rotation may be from a rotation: its rows from unit length.
constexpr float kRigidTolerance = 1e-3F;

/// Whether `transform` is finite and its linear part a rotation, within kRigidTolerance.
[[nodiscard]] auto isRigid(const nx::Mat3x4& transform) noexcept -> bool {
    return std::ranges::all_of(transform.rows, [](nx::Vec4 row) {
        return std::isfinite(row.x) && std::isfinite(row.y) && std::isfinite(row.z) && std::isfinite(row.w) &&
               std::abs(nx::lengthSquared(nx::xyz(row)) - 1.0F) <= kRigidTolerance;
    });
}

/// Poses the bones of `model` at every frame of `sequence`, whose decoded frames are `animation`, and
/// halfway to the next one, at both ends of its first blending: every bone's transform must be rigid.
/// Errors: Invalid.
[[nodiscard]] auto checkPoses(const MdlView& model, const nx::assets::StudioSequence& sequence,
                              const nx::assets::AnimationFrames& animation) -> Result<void, CheckError> {
    constexpr float kHalfway = 0.5F;
    constexpr std::uint8_t kFullBlending = 255;
    const cs::Skeleton skeleton = cs::skeletonOf(model);
    const auto last = static_cast<float>(animation.frameCount - 1);
    for (std::uint32_t frame = 0; frame < animation.frameCount; ++frame) {
        for (const float at : {static_cast<float>(frame), static_cast<float>(frame) + kHalfway}) {
            for (const std::uint8_t blending : {std::uint8_t{0}, kFullBlending}) {
                if (at > last) {
                    continue;
                }
                const std::vector<nx::Mat3x4> pose =
                    cs::poseBones(skeleton, sequence, animation,
                                  {.frame = cs::SequenceFrame{at}, .blending = {blending, 0}});
                if (const auto bone = std::ranges::find_if_not(pose, isRigid); bone != pose.end()) {
                    return fail(
                        CheckError::Invalid,
                        std::format("sequence {:?} at frame {} poses bone {:?} out of a rigid transform",
                                    sequence.label, at,
                                    model.bones()[nx::narrow<std::size_t>(bone - pose.begin())].name));
                }
            }
        }
    }
    return {};
}

/// Checks the model at `path`: its textures, from its `*T.mdl` when it has none, and the animation
/// of every sequence, from the sequence group file it is in, posed at each of its frames.
[[nodiscard]] auto checkMdl(const Vfs& vfs, const VfsPath& path, std::span<const std::byte> bytes)
    -> Result<void, CheckError> {
    auto model = MdlView::parse(bytes);
    if (!model) {
        return fail(CheckError::Invalid, std::move(model.error()));
    }

    if (model->textures().empty()) {
        auto texturesBytes = readJoined(vfs, nx::assets::texturesFilePath(path), "the textures file");
        if (!texturesBytes) {
            return std::unexpected{std::move(texturesBytes.error())};
        }
        auto texturesFile = MdlView::parse(*texturesBytes);
        if (!texturesFile) {
            return fail(CheckError::DependencyFailed, "the textures file", std::move(texturesFile.error()));
        }
        if (auto joined = model->checkTextures(*texturesFile); !joined) {
            return fail(CheckError::DependencyFailed, "the textures file", std::move(joined.error()));
        }
    }

    // Each sequence group file is read once, when its first sequence needs it.
    std::vector<std::optional<std::vector<std::byte>>> groupFiles(model->sequenceGroups().size());
    for (std::uint32_t index = 0; index < model->sequences().size(); ++index) {
        const nx::assets::StudioSequence& sequence = model->sequences()[index];
        std::span<const std::byte> groupFile = bytes;
        if (const std::uint32_t group = sequence.group.value; group != 0) {
            std::optional<std::vector<std::byte>>& file = groupFiles[group];
            if (!file) {
                const std::string_view fileName = model->sequenceGroups()[group].fileName;
                auto read = readJoined(vfs, VfsPath::parseGoldsrc(fileName), "the sequence group file");
                if (!read) {
                    return std::unexpected{std::move(read.error())};
                }
                file = std::move(*read);
            }
            groupFile = *file;
        }
        auto frames = model->decodeAnimation(nx::assets::SequenceIndex{index}, groupFile);
        if (!frames) {
            return fail(CheckError::Invalid, std::format("the animation of sequence {:?}", sequence.label),
                        std::move(frames.error()));
        }
        if (auto posed = checkPoses(*model, sequence, *frames); !posed) {
            return posed;
        }
    }
    return {};
}

[[nodiscard]] auto checkSpr(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    if (auto sprite = SpriteView::parse(bytes); !sprite) {
        return fail(CheckError::Invalid, std::move(sprite.error()));
    }
    return {};
}

[[nodiscard]] auto checkTga(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    if (auto image = nx::assets::readTga(bytes); !image) {
        return fail(CheckError::Invalid, std::move(image.error()));
    }
    return {};
}

[[nodiscard]] auto checkWav(std::span<const std::byte> bytes) -> Result<void, CheckError> {
    if (auto sound = nx::assets::parseWav(bytes); !sound) {
        return fail(CheckError::Invalid, std::move(sound.error()));
    }
    return {};
}

/// The reader of the files with one extension, in one folder when the extension alone does not tell.
struct Reader {
    std::string_view extension;
    std::string_view folder{}; ///< The start of their paths: "sprites/" for the sprite lists.
    Format format{};
    auto (*check)(const Vfs& vfs, const VfsPath& path, std::span<const std::byte> bytes)
        -> Result<void, CheckError>;
};

constexpr auto kReaders = std::to_array<Reader>({
    {.extension = "bsp",
     .format = Format::Bsp,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkBsp(bytes); }},
    {.extension = "wad",
     .format = Format::Wad,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkWad(bytes); }},
    {.extension = "mdl", .format = Format::Mdl, .check = checkMdl},
    {.extension = "spr",
     .format = Format::Spr,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkSpr(bytes); }},
    {.extension = "tga",
     .format = Format::Tga,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkTga(bytes); }},
    {.extension = "wav",
     .format = Format::Wav,
     .check = [](const Vfs&, const VfsPath&, std::span<const std::byte> bytes) { return checkWav(bytes); }},
    {.extension = "txt",
     .folder = "sprites/",
     .format = Format::SpriteList,
     .check = [](const Vfs&, const VfsPath&,
                 std::span<const std::byte> bytes) { return checkSpriteList(bytes); }},
    {.extension = "res",
     .folder = "resource/",
     .format = Format::KeyValues,
     .check =
         [](const Vfs&, const VfsPath& path, std::span<const std::byte> bytes) {
             return isScheme(path) ? checkScheme(bytes) : checkKeyValues(bytes, KeyValuesEscapes::None);
         }},
    {.extension = "res",
     .folder = "classes/",
     .format = Format::KeyValues,
     .check = [](const Vfs&, const VfsPath&,
                 std::span<const std::byte> bytes) { return checkKeyValues(bytes, KeyValuesEscapes::None); }},
    {.extension = "txt",
     .folder = "resource/",
     .format = Format::Localization,
     .check = [](const Vfs&, const VfsPath&,
                 std::span<const std::byte> bytes) { return checkLocalization(bytes); }},
});

/// The reader of the file at `path`, by its extension; null when no reader reads it yet.
[[nodiscard]] auto readerOf(const VfsPath& path) -> const Reader* {
    const std::string_view text = path.text();
    const std::string_view extension = text.substr(text.rfind('.') + 1); // every VfsPath has one
    const auto found = std::ranges::find_if(kReaders, [text, extension](const Reader& reader) {
        return nx::equalsIgnoringCase(extension, reader.extension) &&
               nx::equalsIgnoringCase(text.substr(0, reader.folder.size()), reader.folder);
    });
    return found == kReaders.end() ? nullptr : &*found;
}

/// Reads the file at `path` and checks it with `reader`. A sequence group file has the extension
/// of a model: its content tells them apart.
[[nodiscard]] auto checkFile(const Vfs& vfs, const VfsPath& path, const Reader& reader, FileResult& result)
    -> Result<void, CheckError> {
    result.format = reader.format;
    auto bytes = vfs.readAll(path);
    if (!bytes) {
        return fail(CheckError::ReadFailed, std::move(bytes.error()));
    }
    result.bytes = bytes->size();
    if (reader.format == Format::Mdl && std::ranges::starts_with(*bytes, nx::assets::kSequenceGroupMagic)) {
        result.format = Format::SequenceGroup;
        return {}; // read by the models whose sequences are in it
    }
    if (reader.format == Format::KeyValues && isScheme(path)) {
        result.format = Format::Scheme;
    }
    if (reader.format == Format::Localization && !std::ranges::starts_with(*bytes, kUtf16Mark)) {
        result.format = std::nullopt; // another text of resource/ (BackgroundLayout.txt): no reader yet
        result.bytes = 0;
        return {};
    }
    return reader.check(vfs, path, *bytes);
}

} // namespace

auto checkContent(const Vfs& vfs, nx::jobs::JobSystem& jobs) -> std::vector<FileResult> {
    const std::vector<VfsPath> paths = vfs.paths();
    std::vector<FileResult> results(paths.size());
    const std::span<const VfsPath> pathView = paths;
    const std::span<FileResult> resultView = results;
    const nx::jobs::JobHandle done =
        jobs.parallelFor({.label = "nxcheck.file"}, std::views::iota(std::size_t{0}, paths.size()),
                         nx::jobs::Grain::automatic(), [&vfs, pathView, resultView](std::size_t index) {
                             const VfsPath& path = pathView[index];
                             FileResult& result = resultView[index];
                             result.path = std::string{path.text()};
                             const Reader* reader = readerOf(path);
                             if (reader == nullptr) {
                                 return;
                             }
                             if (auto checked = checkFile(vfs, path, *reader, result); !checked) {
                                 result.failure = std::move(checked.error());
                             }
                         });
    jobs.wait(done);
    return results;
}

} // namespace nxcheck
