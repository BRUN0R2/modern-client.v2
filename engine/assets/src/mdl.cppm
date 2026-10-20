// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// MDL v10, the studio models of GoldSrc: players, weapons, hostages (docs/06-assets-and-resources.md,
/// section 3.1): the skeleton, the bone controllers, the hitboxes, the attachments, the body parts
/// with their geometry, the textures and the skins, the sequences and their animations, in the
/// model or in its sequence groups (`*01.mdl`). Every index is checked against what it indexes,
/// every bone comes after its parent, and every list is a vector: no count is held to the
/// original's ceilings (docs/06, section 3.4).
export module nx.assets:mdl;

import std;
import nx.core;
import nx.math;
import :vfs;
import :wad;

export namespace nx::assets {

/// The studio model version of GoldSrc.
inline constexpr std::int32_t kStudioVersion = 10;

/// The degrees of freedom of a bone: three translations, then three rotations.
inline constexpr std::size_t kBoneAxes = 6;

/// The most values decodeAnimation decodes for one sequence: a safety ceiling, not a content limit
/// (a player's sequence with nine blends decodes to under half a million). A constant axis costs no
/// byte in the file, so without it a hostile frame count could ask for any amount of memory.
inline constexpr std::uint64_t kMaxAnimationValues = std::uint64_t{1} << 24;

/// What can be wrong with an MDL file; the detail names the table, the record and the value.
enum class MdlError : std::uint8_t {
    TooSmall,           ///< Smaller than the header.
    NotStudioModel,     ///< The file does not start with "IDST".
    UnsupportedVersion, ///< Not version 10.
    OutOfBounds,        ///< A table, a record or the data it points to does not fit in the file.
    BadIndex,           ///< A record names one that does not exist.
    BadValue,           ///< A value outside its domain: a float that is not finite, a bone that is not
                        ///< after its parent, a controller of no single axis, a texture of no size, a
                        ///< triangle run of fewer than three corners, a sequence of no frame or blend,
                        ///< an animation run that holds no value, an animation over kMaxAnimationValues.
    NotSequenceGroup,   ///< A sequence group file that does not start with "IDSQ" and version 10.
};

/// An index into one list of an MdlView, named by `Tag`; parse() checks every one it reads.
template <class Tag>
struct StudioIndex {
    std::uint32_t value = 0;

    [[nodiscard]] inline auto operator==(const StudioIndex&) const noexcept -> bool = default;
};

struct BoneTag;
struct BoneControllerTag;
struct SequenceTag;
struct SequenceGroupTag;
struct StudioVertexTag;
struct StudioNormalTag;
struct StudioTextureTag;

using BoneIndex = StudioIndex<BoneTag>;
using BoneControllerIndex = StudioIndex<BoneControllerTag>;
using SequenceIndex = StudioIndex<SequenceTag>;
using SequenceGroupIndex = StudioIndex<SequenceGroupTag>;
using StudioVertexIndex = StudioIndex<StudioVertexTag>;
using StudioNormalIndex = StudioIndex<StudioNormalTag>;
using StudioTextureIndex = StudioIndex<StudioTextureTag>;

/// A degree of freedom: a translation along an axis, or a rotation about it.
enum class BoneAxis : std::uint8_t {
    X,
    Y,
    Z,
    RotationX,
    RotationY,
    RotationZ,
    Count,
};

struct StudioBone {
    std::string name;
    std::optional<BoneIndex> parent{}; ///< Before this bone; none for a root.
    std::array<std::optional<BoneControllerIndex>, kBoneAxes> controllers{}; ///< What drives each axis.
    std::array<float, kBoneAxes> values{}; ///< The rest position and angles (radians), by BoneAxis.
    std::array<float, kBoneAxes> scales{}; ///< What one unit of animation moves each axis by.
};

/// Drives one axis of a bone from a value the entity sets (a head turning, a mouth moving).
struct BoneController {
    BoneIndex bone{};
    BoneAxis axis = BoneAxis::X;
    bool wraps = false; ///< The range loops, as an angle that turns past its end.
    float start = 0.0F;
    float end = 0.0F;
    std::int32_t rest = 0;     ///< The setting it starts at.
    std::uint32_t channel = 0; ///< The entity's controller it follows; the original reserves 4 for the mouth.
};

/// A box on a bone that hits register on.
struct Hitbox {
    BoneIndex bone{};
    std::uint32_t group = 0; ///< The hit group (head, chest...), which the game interprets.
    Aabb bounds{};
};

/// A named point on a bone: where a muzzle flashes or a shell ejects.
struct Attachment {
    std::string name;
    BoneIndex bone{};
    Vec3 origin{};
};

/// How a texture is drawn.
enum class StudioTextureFlag : std::uint8_t {
    FlatShade,
    Chrome,
    Fullbright,
    NoMipmaps,
    Alpha,
    Additive,
    Masked,
    Count,
};

/// A paletted texture: its pixels and palette as views of the file's bytes.
struct StudioTexture {
    std::string name;
    Flags<StudioTextureFlag> flags{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::byte> pixels{};  ///< Palette indices, row by row.
    std::span<const std::byte> palette{}; ///< kPaletteColors RGB triples.
};

/// A vertex or a normal, and the bone it moves with.
struct BoundVector {
    Vec3 vector{};
    BoneIndex bone{};
};

/// A corner of a triangle: a vertex, its normal, and its texture coordinates in pixels.
struct MeshCorner {
    StudioVertexIndex vertex{};
    StudioNormalIndex normal{};
    std::int16_t s = 0;
    std::int16_t t = 0;
};

/// How the corners of a run make triangles.
enum class Winding : std::uint8_t {
    Strip, ///< Each corner after the second closes a triangle with the two before it.
    Fan,   ///< Each corner after the second closes a triangle with the one before it and the first.
};

/// At least three corners.
struct TriangleRun {
    Winding winding = Winding::Strip;
    std::vector<MeshCorner> corners{};
};

/// The triangles of a model that share a texture.
struct StudioMesh {
    std::uint32_t skinReference = 0; ///< Its texture, through the skin a model is drawn with.
    std::vector<TriangleRun> runs{};
};

/// One choice of a body part (a head, a weapon in hand).
struct StudioModel {
    std::string name;
    float boundingRadius = 0.0F;
    std::vector<BoundVector> vertices{};
    std::vector<BoundVector> normals{};
    std::vector<StudioMesh> meshes{};
};

/// A part of the body with its choices; an entity picks one of each part.
struct BodyPart {
    std::string name;
    std::uint32_t base = 0; ///< How the original packs the part's choice into one body number.
    std::vector<StudioModel> models{};
};

/// Something that happens at a frame of a sequence: a footstep, a muzzle flash, a sound.
struct StudioEvent {
    std::int32_t frame = 0;
    std::int32_t event = 0; ///< What happens, which the game interprets.
    std::string options;    ///< Its argument: a sound's name, an attachment's number...
};

/// An animation: its frames, the blends between which it interpolates, and how it moves the model.
struct StudioSequence {
    std::string label;
    float fps = 0.0F;
    bool loops = false;
    std::int32_t activity = 0;       ///< The game's activity it plays for.
    std::int32_t activityWeight = 0; ///< How likely it is chosen among the activity's sequences.
    std::vector<StudioEvent> events{};
    std::uint32_t frameCount = 0; ///< At least one.
    std::uint32_t motionType = 0; ///< The axes its root motion moves along, as the controllers' axis bits.
    BoneIndex motionBone{};       ///< The bone its root motion is taken from.
    Vec3 linearMovement{};        ///< How far the whole sequence moves the model.
    Aabb bounds{};
    std::uint32_t blendCount = 0;              ///< At least one: the animations it interpolates between.
    std::array<std::uint32_t, 2> blendTypes{}; ///< What drives each blend, as the controllers' axis bits.
    std::array<float, 2> blendStarts{};
    std::array<float, 2> blendEnds{};
    SequenceGroupIndex group{};  ///< The file its animation is in: 0 for this one.
    std::uint32_t entryNode = 0; ///< Where it starts and ends in the transition graph.
    std::uint32_t exitNode = 0;
    std::uint32_t nodeFlags = 0;
    std::uint32_t animationOffset = 0; ///< Where its animation starts in its group's file.
};

/// A file the sequences' animations are in: the model itself for group 0, a `*01.mdl` for group 1.
struct SequenceGroup {
    std::string label;
    std::string fileName; ///< The path the original loads it from.
};

/// The decoded frames of a sequence: for every blend, frame and bone, the six raw values of the
/// bone's axes. A pose adds `scales[axis] * value` to a bone's rest values.
struct AnimationFrames {
    std::uint32_t blendCount = 0;
    std::uint32_t frameCount = 0;
    std::uint32_t boneCount = 0;
    std::vector<std::int16_t> values{}; ///< blendCount * frameCount * boneCount * kBoneAxes values.

    /// The value of `axis` of `bone` at `frame` of `blend`.
    [[nodiscard]] auto value(std::uint32_t blend, std::uint32_t frame, BoneIndex bone,
                             BoneAxis axis) const noexcept -> std::int16_t;
};

/// An MDL v10 file, or the textures file (`*T.mdl`) of one: decoded records, and views of the bytes
/// it was parsed from (texture pixels and palettes), which must outlive it.
class MdlView {
public:
    /// Errors: every MdlError.
    [[nodiscard]] static auto parse(std::span<const std::byte> bytes) -> Result<MdlView, MdlError>;

    [[nodiscard]] inline auto name() const noexcept -> std::string_view { return m_name; }
    [[nodiscard]] inline auto eyePosition() const noexcept -> Vec3 { return m_eyePosition; }
    /// The box the model's movement and culling use.
    [[nodiscard]] inline auto bounds() const noexcept -> const Aabb& { return m_bounds; }
    [[nodiscard]] inline auto bones() const noexcept -> std::span<const StudioBone> { return m_bones; }
    [[nodiscard]] inline auto boneControllers() const noexcept -> std::span<const BoneController> {
        return m_boneControllers;
    }
    [[nodiscard]] inline auto hitboxes() const noexcept -> std::span<const Hitbox> { return m_hitboxes; }
    [[nodiscard]] inline auto attachments() const noexcept -> std::span<const Attachment> {
        return m_attachments;
    }
    /// Empty when the textures are in the model's `*T.mdl`.
    [[nodiscard]] inline auto textures() const noexcept -> std::span<const StudioTexture> {
        return m_textures;
    }
    /// Each skin family maps every skin reference to a texture; empty with the textures.
    [[nodiscard]] inline auto skins() const noexcept -> std::span<const std::vector<StudioTextureIndex>> {
        return m_skins;
    }
    [[nodiscard]] inline auto bodyParts() const noexcept -> std::span<const BodyPart> { return m_bodyParts; }
    [[nodiscard]] inline auto sequences() const noexcept -> std::span<const StudioSequence> {
        return m_sequences;
    }
    [[nodiscard]] inline auto sequenceGroups() const noexcept -> std::span<const SequenceGroup> {
        return m_sequenceGroups;
    }
    /// For every pair of transition nodes, the node to go through next; a view of the file's bytes.
    [[nodiscard]] inline auto transitions() const noexcept -> std::span<const std::byte> {
        return m_transitions;
    }

    /// Checks that `texturesFile`, this model's `*T.mdl` (texturesFilePath), has the textures and
    /// skins every mesh of this model draws with. Errors: BadIndex (a skin reference past its
    /// skins), BadValue (it has no texture).
    [[nodiscard]] auto checkTextures(const MdlView& texturesFile) const -> Result<void, MdlError>;

    /// Decodes the animation of `sequence` from `groupFile`: the bytes this model was parsed from for
    /// group 0, the sequence group's file (`*0N.mdl`) for the others. Every run-length stream is
    /// checked to the last frame. Errors: BadIndex (no such sequence), NotSequenceGroup,
    /// OutOfBounds, BadValue.
    [[nodiscard]] auto decodeAnimation(SequenceIndex sequence, std::span<const std::byte> groupFile) const
        -> Result<AnimationFrames, MdlError>;

private:
    MdlView() = default;

    /// Reads the sequence groups, the sequences and the transitions.
    [[nodiscard]] auto decodeSequences(std::span<const std::byte> bytes) -> std::optional<Failure<MdlError>>;

    std::string m_name;
    Vec3 m_eyePosition{};
    Aabb m_bounds{};
    std::vector<StudioBone> m_bones;
    std::vector<BoneController> m_boneControllers;
    std::vector<Hitbox> m_hitboxes;
    std::vector<Attachment> m_attachments;
    std::vector<StudioTexture> m_textures;
    std::vector<std::vector<StudioTextureIndex>> m_skins;
    std::vector<BodyPart> m_bodyParts;
    std::vector<StudioSequence> m_sequences;
    std::vector<SequenceGroup> m_sequenceGroups;
    std::span<const std::byte> m_transitions;
};

/// The `*T.mdl` that holds the textures of the model at `model`, when the model has none:
/// "models/player.mdl" has them in "models/playerT.mdl". Errors: InvalidPath (a model path that
/// does not end in ".mdl").
[[nodiscard]] auto texturesFilePath(const VfsPath& model) -> Result<VfsPath, VfsError>;

} // namespace nx::assets
