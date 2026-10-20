// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Typed queries (docs/03-ecs.md, section 6).
///
/// The terms say what a query reads and writes: `const T&` reads T, `T&` writes T, `Optional<T>`
/// accesses T when the entity has it, `Entity` gives the handle. Filters only select:
/// `With<T>`, `Without<T>` and `Changed<T>`.
export module nx.ecs:query;

import std;
import nx.core;
import nx.jobs;
import :archetype;
import :component;
import :entity;
import :world;

export namespace nx::ecs {

/// Filter: the entity has T (typical for tags).
template <Component Type>
struct With {};

/// Filter: the entity lacks T.
template <Component Type>
struct Without {};

/// Filter: the entity has T, in a chunk whose T column was written since the query's last run.
/// Chunk-grained and conservative: a mutable access or a structural change in the chunk counts.
template <Component Type>
struct Changed {};

/// Access to a component the entity may lack: the function receives `T*`, nullptr when absent.
/// `Optional<const T>` reads, `Optional<T>` writes.
template <class Type>
struct Optional {};

namespace detail {

template <class>
inline constexpr bool kAlwaysFalse = false;

enum class TermKind : std::uint8_t {
    Read,
    Write,
    OptionalRead,
    OptionalWrite,
    EntityHandle,
    With,
    Without,
    Changed,
};

/// How each kind of term matches archetypes and what it hands to the query function.
template <class Term>
struct QueryTerm {
    static_assert(kAlwaysFalse<Term>, "not a query term: use const T&, T&, Optional<T>, Entity, With<T>, "
                                      "Without<T> or Changed<T>");
};

template <class Term>
concept StoredComponent = Component<Term> && !TagComponent<Term>;

template <class Type>
[[nodiscard]] auto elementAt(std::byte* base, std::uint32_t row) noexcept -> Type* {
    return std::launder(static_cast<Type*>(static_cast<void*>(base + (std::size_t{row} * sizeof(Type)))));
}

template <class Type>
[[nodiscard]] auto arrayAt(std::byte* base, std::uint32_t count) noexcept -> std::span<Type> {
    return {std::launder(static_cast<Type*>(static_cast<void*>(base))), count};
}

template <StoredComponent Type>
struct QueryTerm<const Type&> {
    using ComponentType = Type;
    static constexpr TermKind kKind = TermKind::Read;
    static auto row(std::byte* base, std::uint32_t row) noexcept -> const Type& {
        return *elementAt<const Type>(base, row);
    }
    static auto column(std::byte* base, std::uint32_t count) noexcept -> std::span<const Type> {
        return arrayAt<const Type>(base, count);
    }
};

template <StoredComponent Type>
struct QueryTerm<Type&> {
    using ComponentType = Type;
    static constexpr TermKind kKind = TermKind::Write;
    static auto row(std::byte* base, std::uint32_t row) noexcept -> Type& {
        return *elementAt<Type>(base, row);
    }
    static auto column(std::byte* base, std::uint32_t count) noexcept -> std::span<Type> {
        return arrayAt<Type>(base, count);
    }
};

template <StoredComponent Type>
struct QueryTerm<Optional<const Type>> {
    using ComponentType = Type;
    static constexpr TermKind kKind = TermKind::OptionalRead;
    static auto row(std::byte* base, std::uint32_t row) noexcept -> const Type* {
        return base != nullptr ? elementAt<const Type>(base, row) : nullptr;
    }
    /// Empty when the archetype lacks the component.
    static auto column(std::byte* base, std::uint32_t count) noexcept -> std::span<const Type> {
        return base != nullptr ? arrayAt<const Type>(base, count) : std::span<const Type>{};
    }
};

template <StoredComponent Type>
struct QueryTerm<Optional<Type>> {
    using ComponentType = Type;
    static constexpr TermKind kKind = TermKind::OptionalWrite;
    static auto row(std::byte* base, std::uint32_t row) noexcept -> Type* {
        return base != nullptr ? elementAt<Type>(base, row) : nullptr;
    }
    static auto column(std::byte* base, std::uint32_t count) noexcept -> std::span<Type> {
        return base != nullptr ? arrayAt<Type>(base, count) : std::span<Type>{};
    }
};

template <>
struct QueryTerm<Entity> {
    using ComponentType = void;
    static constexpr TermKind kKind = TermKind::EntityHandle;
    static auto row(std::byte* base, std::uint32_t row) noexcept -> Entity {
        return *elementAt<const Entity>(base, row);
    }
    static auto column(std::byte* base, std::uint32_t count) noexcept -> std::span<const Entity> {
        return arrayAt<const Entity>(base, count);
    }
};

template <TagComponent Type>
struct QueryTerm<const Type&> {
    static_assert(kAlwaysFalse<Type>, "tags have no data to access: select them with With<T>");
};

template <TagComponent Type>
struct QueryTerm<Type&> {
    static_assert(kAlwaysFalse<Type>, "tags have no data to access: select them with With<T>");
};

template <Component Type>
struct QueryTerm<With<Type>> {
    using ComponentType = Type;
    static constexpr TermKind kKind = TermKind::With;
};

template <Component Type>
struct QueryTerm<Without<Type>> {
    using ComponentType = Type;
    static constexpr TermKind kKind = TermKind::Without;
};

template <StoredComponent Type>
struct QueryTerm<Changed<Type>> {
    using ComponentType = Type;
    static constexpr TermKind kKind = TermKind::Changed;
};

[[nodiscard]] constexpr auto isAccess(TermKind kind) noexcept -> bool {
    return kind == TermKind::Read || kind == TermKind::Write || kind == TermKind::OptionalRead ||
           kind == TermKind::OptionalWrite || kind == TermKind::EntityHandle;
}

[[nodiscard]] constexpr auto isWrite(TermKind kind) noexcept -> bool {
    return kind == TermKind::Write || kind == TermKind::OptionalWrite;
}

/// Positions of the access terms among all the terms, in order.
template <class... Terms>
[[nodiscard]] consteval auto accessPositions() noexcept {
    constexpr std::array<TermKind, sizeof...(Terms)> kinds{QueryTerm<Terms>::kKind...};
    constexpr auto count = static_cast<std::size_t>(std::ranges::count_if(kinds, isAccess));
    std::array<std::size_t, count> positions{};
    std::size_t next = 0;
    for (std::size_t term = 0; term < kinds.size(); ++term) {
        if (isAccess(kinds[term])) {
            positions[next++] = term;
        }
    }
    return positions;
}

/// Two terms that access the same component would hand out aliasing references.
template <class... Terms>
[[nodiscard]] consteval auto accessesAreDistinct() noexcept -> bool {
    constexpr std::array<TermKind, sizeof...(Terms)> kinds{QueryTerm<Terms>::kKind...};
    constexpr std::array<const void*, sizeof...(Terms)> keys{
        typeKeyOf<typename QueryTerm<Terms>::ComponentType>()...};
    for (std::size_t first = 0; first < kinds.size(); ++first) {
        for (std::size_t second = first + 1; second < kinds.size(); ++second) {
            if (isAccess(kinds[first]) && isAccess(kinds[second]) && kinds[first] != TermKind::EntityHandle &&
                keys[first] == keys[second]) {
                return false;
            }
        }
    }
    return true;
}

} // namespace detail

/// Entities that match a set of terms, and the components the terms access.
///
/// The query remembers the archetypes it matched and only examines archetypes created since its
/// previous use. `lastRun` is the version a Changed<T> filter compares against; `thisRun` is the
/// version written accesses stamp on chunks (the scheduler gives each system run its own).
template <class... Terms>
class Query {
    static constexpr std::size_t kTermCount = sizeof...(Terms);
    static constexpr std::array<detail::TermKind, kTermCount> kKinds{detail::QueryTerm<Terms>::kKind...};
    static constexpr auto kAccessPositions = detail::accessPositions<Terms...>();
    static constexpr std::size_t kAccessCount = kAccessPositions.size();

    template <std::size_t Position>
    using TermAt = detail::QueryTerm<std::tuple_element_t<Position, std::tuple<Terms...>>>;

    template <std::size_t Access>
    using AccessTerm = TermAt<kAccessPositions[Access]>;

    template <class Sequence>
    struct RowTypes;
    template <std::size_t... Access>
    struct RowTypes<std::index_sequence<Access...>> {
        using Tuple = std::tuple<decltype(AccessTerm<Access>::row(nullptr, 0))...>;
        template <class Function>
        static constexpr bool kInvocableByRow =
            std::invocable<Function&, decltype(AccessTerm<Access>::row(nullptr, 0))...>;
        template <class Function>
        static constexpr bool kInvocableByChunk =
            std::invocable<Function&, decltype(AccessTerm<Access>::column(nullptr, 0))...>;
    };
    using Rows = RowTypes<std::make_index_sequence<kAccessCount>>;

    static_assert(detail::accessesAreDistinct<Terms...>(),
                  "a query may access each component through one term only");

public:
    /// Tuple of what one row hands to forEach: `const T&`, `T&`, `T*` or Entity per access term.
    using RowTuple = typename Rows::Tuple;

    explicit Query(World& world) : Query{world, Version{0}, world.version()} {}

    Query(World& world, Version lastRun, Version thisRun)
        : m_world{&world}, m_lastRun{lastRun}, m_thisRun{thisRun} {
        resolveComponentIds(std::make_index_sequence<kTermCount>{});
    }

    /// Calls `function` with the accessed values of every matching entity, chunk after chunk.
    template <class Function>
        requires Rows::template
    kInvocableByRow<Function> void forEach(Function&& function) {
        refresh();
        const World::IterationScope scope{*m_world};
        for (Match& match : m_matches) {
            for (std::size_t chunk = 0; chunk < match.archetype->chunkCount(); ++chunk) {
                if (const auto bases = enterChunk(match, chunk)) {
                    const std::uint32_t rowCount = match.archetype->chunk(chunk).rowCount();
                    for (std::uint32_t row = 0; row < rowCount; ++row) {
                        invokeRow(function, *bases, row, std::make_index_sequence<kAccessCount>{});
                    }
                }
            }
        }
    }

    /// Calls `function` once per matching chunk with one span per access term: the column arrays,
    /// ready for loops the compiler can vectorize.
    template <class Function>
        requires Rows::template
    kInvocableByChunk<Function> void forEachChunk(Function&& function) {
        refresh();
        const World::IterationScope scope{*m_world};
        for (Match& match : m_matches) {
            for (std::size_t chunk = 0; chunk < match.archetype->chunkCount(); ++chunk) {
                if (const auto bases = enterChunk(match, chunk)) {
                    invokeChunk(function, *bases, match.archetype->chunk(chunk).rowCount(),
                                std::make_index_sequence<kAccessCount>{});
                }
            }
        }
    }

    /// forEachChunk with the chunks spread over jobs; returns when every chunk is done. `function`
    /// runs on several threads at once, each call on a different chunk.
    template <class Function>
        requires Rows::template
    kInvocableByChunk<Function> void parallelForEachChunk(jobs::JobSystem& jobs, Function&& function,
                                                          jobs::JobLabel label = "ecs.forEachChunk") {
        refresh();
        const World::IterationScope scope{*m_world};
        std::vector<ChunkRef> chunks;
        for (Match& match : m_matches) {
            for (std::size_t chunk = 0; chunk < match.archetype->chunkCount(); ++chunk) {
                if (passesChunkFilters(match, match.archetype->chunk(chunk))) {
                    chunks.push_back(ChunkRef{.match = &match, .chunk = chunk});
                }
            }
        }
        jobs.wait(jobs.parallelFor(
            {.label = label}, chunks, jobs::Grain::automatic(), [this, &function](const ChunkRef& ref) {
                const auto bases = enterChunk(*ref.match, ref.chunk);
                invokeChunk(function, *bases, ref.match->archetype->chunk(ref.chunk).rowCount(),
                            std::make_index_sequence<kAccessCount>{});
            }));
    }

    /// The accessed values of one entity, or nullopt when it is dead or does not match.
    [[nodiscard]] auto get(Entity entity) -> std::optional<RowTuple> {
        refresh();
        const auto location = m_world->findLocation(entity);
        if (!location) {
            return std::nullopt;
        }
        const auto archetypeIndex = std::size_t{std::to_underlying(location->archetype)};
        if (archetypeIndex >= m_matchByArchetype.size() || !m_matchByArchetype[archetypeIndex].has_value()) {
            return std::nullopt;
        }
        Match& match = m_matches[*m_matchByArchetype[archetypeIndex]];
        const auto bases = enterChunk(match, location->chunk);
        if (!bases) {
            return std::nullopt;
        }
        return rowTuple(*bases, location->row, std::make_index_sequence<kAccessCount>{});
    }

    /// Matching entities (after the chunk filters).
    [[nodiscard]] auto count() -> std::size_t {
        refresh();
        std::size_t total = 0;
        for (Match& match : m_matches) {
            for (std::size_t chunk = 0; chunk < match.archetype->chunkCount(); ++chunk) {
                const Chunk& stored = match.archetype->chunk(chunk);
                if (passesChunkFilters(match, stored)) {
                    total += stored.rowCount();
                }
            }
        }
        return total;
    }

private:
    using Bases = std::array<std::byte*, kTermCount>;

    struct Match {
        Archetype* archetype = nullptr;
        /// Column of each term's component in this archetype, when it stores it.
        std::array<std::optional<std::size_t>, kTermCount> columns{};
    };

    struct ChunkRef {
        Match* match = nullptr;
        std::size_t chunk = 0;
    };

    template <std::size_t... Position>
    void resolveComponentIds(std::index_sequence<Position...> /*positions*/) {
        ((m_ids[Position] = resolveComponentId<typename TermAt<Position>::ComponentType>()), ...);
    }

    /// An id never registered when the type is not registered (already reported): the query then
    /// matches nothing that needs it.
    template <class Type>
    [[nodiscard]] auto resolveComponentId() const -> ComponentTypeId {
        if constexpr (std::is_void_v<Type>) {
            return kNoComponent;
        } else {
            return m_world->requireComponentId<Type>().value_or(kNoComponent);
        }
    }

    void refresh() {
        for (; m_archetypesSeen < m_world->archetypeCount(); ++m_archetypesSeen) {
            Archetype& archetype = m_world->archetype(ArchetypeId{narrow<std::uint32_t>(m_archetypesSeen)});
            if (!matches(archetype)) {
                continue;
            }
            Match match{.archetype = &archetype};
            for (std::size_t term = 0; term < kTermCount; ++term) {
                if (kKinds[term] != detail::TermKind::EntityHandle) {
                    match.columns[term] = archetype.columnOf(m_ids[term]);
                }
            }
            m_matchByArchetype.resize(m_archetypesSeen + 1);
            m_matchByArchetype[m_archetypesSeen] = m_matches.size();
            m_matches.push_back(match);
        }
    }

    [[nodiscard]] auto matches(const Archetype& archetype) const noexcept -> bool {
        for (std::size_t term = 0; term < kTermCount; ++term) {
            switch (kKinds[term]) {
            case detail::TermKind::Read:
            case detail::TermKind::Write:
            case detail::TermKind::With:
            case detail::TermKind::Changed:
                if (!archetype.contains(m_ids[term])) {
                    return false;
                }
                break;
            case detail::TermKind::Without:
                if (archetype.contains(m_ids[term])) {
                    return false;
                }
                break;
            case detail::TermKind::OptionalRead:
            case detail::TermKind::OptionalWrite:
            case detail::TermKind::EntityHandle:
                break;
            }
        }
        return true;
    }

    [[nodiscard]] auto passesChunkFilters(const Match& match, const Chunk& chunk) const noexcept -> bool {
        for (std::size_t term = 0; term < kTermCount; ++term) {
            if (kKinds[term] == detail::TermKind::Changed &&
                chunk.changedVersion(*match.columns[term]) <= m_lastRun) {
                return false;
            }
        }
        return chunk.rowCount() != 0;
    }

    /// Column bases of a chunk that passes the filters, after stamping the written columns.
    [[nodiscard]] auto enterChunk(Match& match, std::size_t chunkIndex) -> std::optional<Bases> {
        Chunk& chunk = match.archetype->chunk(chunkIndex);
        if (!passesChunkFilters(match, chunk)) {
            return std::nullopt;
        }
        Bases bases{};
        for (std::size_t term = 0; term < kTermCount; ++term) {
            if (kKinds[term] == detail::TermKind::EntityHandle) {
                bases[term] = match.archetype->entities(chunkIndex);
            } else if (detail::isAccess(kKinds[term]) && match.columns[term].has_value()) {
                bases[term] = match.archetype->columnData(chunkIndex, *match.columns[term]);
                if (detail::isWrite(kKinds[term])) {
                    chunk.markChanged(*match.columns[term], m_thisRun);
                }
            }
        }
        return bases;
    }

    template <class Function, std::size_t... Access>
    static void invokeRow(Function& function, const Bases& bases, std::uint32_t row,
                          std::index_sequence<Access...> /*access*/) {
        std::invoke(function, AccessTerm<Access>::row(bases[kAccessPositions[Access]], row)...);
    }

    template <class Function, std::size_t... Access>
    static void invokeChunk(Function& function, const Bases& bases, std::uint32_t rowCount,
                            std::index_sequence<Access...> /*access*/) {
        std::invoke(function, AccessTerm<Access>::column(bases[kAccessPositions[Access]], rowCount)...);
    }

    template <std::size_t... Access>
    [[nodiscard]] static auto rowTuple(const Bases& bases, std::uint32_t row,
                                       std::index_sequence<Access...> /*access*/) -> RowTuple {
        return RowTuple{AccessTerm<Access>::row(bases[kAccessPositions[Access]], row)...};
    }

    static constexpr ComponentTypeId kNoComponent{std::numeric_limits<std::uint32_t>::max()};

    World* m_world;
    Version m_lastRun;
    Version m_thisRun;
    std::array<ComponentTypeId, kTermCount> m_ids{};
    std::vector<Match> m_matches;
    std::vector<std::optional<std::size_t>> m_matchByArchetype; // by ArchetypeId
    std::size_t m_archetypesSeen = 0;
};

} // namespace nx::ecs
