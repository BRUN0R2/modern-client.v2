// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Systems: functions whose parameters say what they access (docs/03-ecs.md, section 7.1).
///
/// A system is any function or non-generic lambda that returns void and takes only system
/// parameters: `Query<...>&` (or by value, which copies the query each run), `Res<const T>`,
/// `Res<T>`, `EventReader<E>`, `EventWriter<E>`, `Commands&`, `World&` (an exclusive system) and
/// `nx::jobs::JobSystem&`.
export module nx.ecs:system;

import std;
import nx.core;
import nx.jobs;
import :access;
import :archetype;
import :commands;
import :component;
import :event;
import :query;
import :world;

export namespace nx::ecs {

/// Access to the resource `Type` in a system: `Res<const T>` reads, `Res<T>` writes.
template <class Type>
class Res {
public:
    explicit Res(Type& value) noexcept : m_value{&value} {}

    [[nodiscard]] auto operator->() const noexcept -> Type* { return m_value; }
    [[nodiscard]] auto operator*() const noexcept -> Type& { return *m_value; }

private:
    Type* m_value;
};

/// What one run of a system gets: the world, the job system and its change versions.
struct SystemRun {
    World& world;
    jobs::JobSystem& jobs;
    Version lastRun;
    Version thisRun;
};

/// A system as the schedule sees it.
class System {
public:
    System() = default;
    System(const System&) = delete;
    auto operator=(const System&) -> System& = delete;
    System(System&&) = delete;
    auto operator=(System&&) -> System& = delete;
    virtual ~System() = default;

    [[nodiscard]] virtual auto access() const noexcept -> const SystemAccess& = 0;
    virtual void run(SystemRun& run) = 0;
    /// Applies the command buffer of the last run, if the system has one.
    virtual auto applyCommands(World& world) -> CommandStats = 0;
};

namespace detail {

struct NoState {};

/// How each parameter type of a system is declared, kept between runs and handed to the function.
template <class Param>
struct SystemParam {
    static_assert(kAlwaysFalse<Param>,
                  "not a system parameter: use Query<...>&, Res<const T>, Res<T>, "
                  "EventReader<E>, EventWriter<E>, Commands&, World& or nx::jobs::JobSystem&");
};

template <class... Terms>
struct SystemParam<Query<Terms...>&> {
    using State = Query<Terms...>;
    static auto makeState(World& world) -> State { return State{world}; }
    static void declare(const State& state, SystemAccess& access) { state.declareAccess(access); }
    static auto isAvailable(const State& /*state*/, const World& /*world*/) noexcept -> bool { return true; }
    static auto fetch(State& state, SystemRun& run) noexcept -> Query<Terms...>& {
        state.setRunVersions(run.lastRun, run.thisRun);
        return state;
    }
};

template <class... Terms>
struct SystemParam<Query<Terms...>> : SystemParam<Query<Terms...>&> {
    static auto fetch(Query<Terms...>& state, SystemRun& run) -> Query<Terms...> {
        state.setRunVersions(run.lastRun, run.thisRun);
        return state;
    }
};

template <Resource Type>
struct SystemParam<Res<const Type>> {
    using State = NoState;
    static auto makeState(World& /*world*/) noexcept -> State { return {}; }
    static void declare(const State& /*state*/, SystemAccess& access) {
        access.resourceReads.push_back(typeKeyOf<Type>());
    }
    static auto isAvailable(const State& /*state*/, const World& world) noexcept -> bool {
        return world.resource<Type>() != nullptr;
    }
    static auto fetch(State& /*state*/, SystemRun& run) noexcept -> Res<const Type> {
        return Res<const Type>{*run.world.resource<Type>()};
    }
};

template <Resource Type>
struct SystemParam<Res<Type>> {
    using State = NoState;
    static auto makeState(World& /*world*/) noexcept -> State { return {}; }
    static void declare(const State& /*state*/, SystemAccess& access) {
        access.resourceWrites.push_back(typeKeyOf<Type>());
    }
    static auto isAvailable(const State& /*state*/, const World& world) noexcept -> bool {
        return world.resource<Type>() != nullptr;
    }
    static auto fetch(State& /*state*/, SystemRun& run) noexcept -> Res<Type> {
        return Res<Type>{*run.world.resource<Type>()};
    }
};

template <Event Type>
struct SystemParam<EventWriter<Type>> {
    using State = NoState;
    static auto makeState(World& /*world*/) noexcept -> State { return {}; }
    static void declare(const State& /*state*/, SystemAccess& access) {
        access.resourceWrites.push_back(typeKeyOf<Events<Type>>());
    }
    static auto isAvailable(const State& /*state*/, const World& world) noexcept -> bool {
        return world.resource<Events<Type>>() != nullptr;
    }
    static auto fetch(State& /*state*/, SystemRun& run) noexcept -> EventWriter<Type> {
        return EventWriter<Type>{*run.world.resource<Events<Type>>()};
    }
};

template <Event Type>
struct SystemParam<EventReader<Type>> {
    using State = std::uint64_t; // sequence number of the first unread event
    static auto makeState(World& /*world*/) noexcept -> State { return 0; }
    static void declare(const State& /*state*/, SystemAccess& access) {
        access.resourceReads.push_back(typeKeyOf<Events<Type>>());
    }
    static auto isAvailable(const State& /*state*/, const World& world) noexcept -> bool {
        return world.resource<Events<Type>>() != nullptr;
    }
    static auto fetch(State& cursor, SystemRun& run) noexcept -> EventReader<Type> {
        return EventReader<Type>{*run.world.resource<Events<Type>>(), cursor};
    }
};

template <>
struct SystemParam<Commands&> {
    using State = Commands;
    static inline auto makeState(World& world) noexcept -> State { return Commands{world}; }
    static inline void declare(const State& /*state*/, SystemAccess& /*access*/) noexcept {
    } // a buffer per system
    static inline auto isAvailable(const State& /*state*/, const World& /*world*/) noexcept -> bool {
        return true;
    }
    static inline auto fetch(State& state, SystemRun& /*run*/) noexcept -> Commands& { return state; }
};

template <>
struct SystemParam<World&> {
    using State = NoState;
    static inline auto makeState(World& /*world*/) noexcept -> State { return {}; }
    static inline void declare(const State& /*state*/, SystemAccess& access) noexcept {
        access.isExclusive = true;
    }
    static inline auto isAvailable(const State& /*state*/, const World& /*world*/) noexcept -> bool {
        return true;
    }
    static inline auto fetch(State& /*state*/, SystemRun& run) noexcept -> World& { return run.world; }
};

template <>
struct SystemParam<jobs::JobSystem&> {
    using State = NoState;
    static inline auto makeState(World& /*world*/) noexcept -> State { return {}; }
    static inline void declare(const State& /*state*/, SystemAccess& /*access*/) noexcept {}
    static inline auto isAvailable(const State& /*state*/, const World& /*world*/) noexcept -> bool {
        return true;
    }
    static inline auto fetch(State& /*state*/, SystemRun& run) noexcept -> jobs::JobSystem& {
        return run.jobs;
    }
};

template <class... Params>
struct ParamList {};

/// Parameter types of a system function or lambda, which must return void.
template <class Function>
struct SystemSignature : SystemSignature<decltype(&Function::operator())> {};

template <class... Params>
struct SystemSignature<void (*)(Params...)> {
    using Parameters = ParamList<Params...>;
};
template <class... Params>
struct SystemSignature<void (*)(Params...) noexcept> : SystemSignature<void (*)(Params...)> {};
template <class Class, class... Params>
struct SystemSignature<void (Class::*)(Params...) const> : SystemSignature<void (*)(Params...)> {};
template <class Class, class... Params>
struct SystemSignature<void (Class::*)(Params...) const noexcept> : SystemSignature<void (*)(Params...)> {};
template <class Class, class... Params>
struct SystemSignature<void (Class::*)(Params...)> : SystemSignature<void (*)(Params...)> {};
template <class Class, class... Params>
struct SystemSignature<void (Class::*)(Params...) noexcept> : SystemSignature<void (*)(Params...)> {};

/// A system made from a function: its parameters' states live here between runs.
template <class Function, class... Params>
class FunctionSystem final : public System {
public:
    FunctionSystem(World& world, Function function)
        : m_function{std::move(function)}, m_states{SystemParam<Params>::makeState(world)...} {
        declareAll(std::index_sequence_for<Params...>{});
    }

    [[nodiscard]] auto access() const noexcept -> const SystemAccess& override { return m_access; }

    void run(SystemRun& run) override { runWith(run, std::index_sequence_for<Params...>{}); }

    auto applyCommands(World& world) -> CommandStats override {
        return applyAll(world, std::index_sequence_for<Params...>{});
    }

private:
    template <std::size_t... Index>
    void declareAll(std::index_sequence<Index...> /*indices*/) {
        (SystemParam<Params>::declare(std::get<Index>(m_states), m_access), ...);
    }

    template <std::size_t... Index>
    void runWith(SystemRun& run, std::index_sequence<Index...> /*indices*/) {
        const bool isAvailable =
            (SystemParam<Params>::isAvailable(std::get<Index>(m_states), run.world) && ...);
        expects(isAvailable, "system: a resource or event queue it takes does not exist in the world");
        if (isAvailable) {
            std::invoke(m_function, SystemParam<Params>::fetch(std::get<Index>(m_states), run)...);
        }
    }

    template <std::size_t... Index>
    auto applyAll(World& world, std::index_sequence<Index...> /*indices*/) -> CommandStats {
        CommandStats total;
        (applyOne(std::get<Index>(m_states), world, total), ...);
        return total;
    }

    template <class State>
    static void applyOne(State& state, World& world, CommandStats& total) {
        if constexpr (std::same_as<State, Commands>) {
            const CommandStats applied = state.apply(world);
            total.applied += applied.applied;
            total.skipped += applied.skipped;
        }
    }

    Function m_function;
    std::tuple<typename SystemParam<Params>::State...> m_states;
    SystemAccess m_access;
};

template <class Function, class... Params>
[[nodiscard]] auto makeSystem(World& world, Function function, ParamList<Params...> /*params*/)
    -> std::unique_ptr<System> {
    return std::make_unique<FunctionSystem<Function, Params...>>(world, std::move(function));
}

} // namespace detail

} // namespace nx::ecs
