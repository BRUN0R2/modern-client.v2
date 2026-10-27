// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.render;
import nx.game;

namespace cs {

namespace {

/// The numbers `_light` may have: red, green, blue and brightness.
constexpr std::size_t kLightNumbers = 4;
constexpr std::size_t kChannels = 3;
/// The brightest a channel or the brightness of `_light` is.
constexpr double kBrightest = 255.0;
/// The game turns the compilers' light into the engine's: pow(level / kCompilerLevel,
/// kLightExponent) * kEngineLevel, the engine's normal brightness.
constexpr double kCompilerLevel = 114.0;
constexpr double kLightExponent = 0.6;
constexpr double kEngineLevel = 264.0;
/// The integers at the start of `text`, each after spaces, as many as `_light` may have, as sscanf's
/// "%d"s read them.
[[nodiscard]] auto leadingIntegers(std::string_view text) -> std::vector<int> {
    constexpr std::string_view kSpaces = " \t\n\v\f\r";
    std::vector<int> numbers;
    while (numbers.size() < kLightNumbers) {
        const std::size_t start = text.find_first_not_of(kSpaces);
        if (start == std::string_view::npos) {
            break;
        }
        text.remove_prefix(start);
        if (text.starts_with('+')) {
            text.remove_prefix(1);
        }
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{}) {
            break;
        }
        numbers.push_back(value);
        text.remove_prefix(nx::narrow<std::size_t>(end - text.data()));
    }
    return numbers;
}

/// The sky's color of a `_light` value: one gray level, its three channels, or its three channels
/// scaled by its brightness, each cut to a whole number; the channels it lacks are 0. Each then
/// becomes the engine's light, cut to a whole number.
[[nodiscard]] auto skyColorOf(std::string_view light) -> nx::Vec3 {
    const std::vector<int> numbers = leadingIntegers(light);
    std::array<double, kChannels> channels{};
    for (std::size_t channel = 0; channel < std::min(numbers.size(), kChannels); ++channel) {
        channels.at(channel) = static_cast<double>(numbers[channel]);
    }
    if (numbers.size() == 1) {
        channels.fill(channels[0]);
    } else if (numbers.size() == kLightNumbers) {
        const double brightness = static_cast<double>(numbers.back()) / kBrightest;
        for (double& channel : channels) {
            channel = std::trunc(channel * brightness);
        }
    }
    const auto engineLight = [](double level) {
        return static_cast<float>(
            std::trunc(std::pow(std::max(level, 0.0) / kCompilerLevel, kLightExponent) * kEngineLevel));
    };
    return {.x = engineLight(channels[0]), .y = engineLight(channels[1]), .z = engineLight(channels[2])};
}

} // namespace

auto highlightLightsOf(const nx::assets::BspView& map) -> std::vector<nx::render::StudioLight> {
    // The compilers' light without a `_light`: white, of 300.
    constexpr std::array<float, 4> kDefaultLight{255.0F, 255.0F, 255.0F, 300.0F};
    constexpr float kChannelTop = 255.0F;
    constexpr float kDisplayGamma = 2.2F;
    // How far a light of a brightness reaches, in units a brightness; and how bright its highlight is.
    constexpr float kReachPerBrightness = 2.0F;
    constexpr float kHighlightPerBrightness = 8.0F;
    std::vector<nx::render::StudioLight> lights;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        const std::optional<std::string_view> classname = entity.find("classname");
        if (classname != "light" && classname != "light_spot") {
            continue;
        }
        const std::optional<nx::Vec3> origin = entity.findVector("origin");
        if (!origin) {
            continue;
        }
        std::array<float, 4> light = kDefaultLight;
        if (const std::optional<std::string_view> value = entity.find("_light")) {
            std::size_t read = 0;
            for (const auto word : std::views::split(*value, ' ')) {
                const std::string_view number{word.begin(), word.end()};
                if (number.empty() || read == light.size()) {
                    continue;
                }
                light.at(read++) = nx::assets::numberOfValue(number).value_or(0.0F);
            }
        }
        const float brightness = light[3];
        if (brightness <= 0.0F) {
            continue;
        }
        const auto linear = [&](float channel) {
            return std::pow(std::clamp(channel / kChannelTop, 0.0F, 1.0F), kDisplayGamma) * brightness *
                   kHighlightPerBrightness;
        };
        lights.push_back(nx::render::StudioLight{
            .position = *origin,
            .reach = brightness * kReachPerBrightness,
            .color = {.x = linear(light[0]), .y = linear(light[1]), .z = linear(light[2])}});
    }
    return lights;
}

auto probePositionsOf(const nx::assets::BspView& map) -> std::vector<nx::Vec3> {
    // How far apart two probes are at least, and how high a spawn's eye is above its origin.
    constexpr float kProbeSpacing = 384.0F;
    constexpr float kEyeHeight = 17.0F;
    // An empty leaf this small holds no probe: a crack between brushes.
    constexpr float kLeastLeafSide = 64.0F;
    std::vector<nx::Vec3> positions;
    const auto add = [&](nx::Vec3 point) {
        if (positions.size() >= nx::render::kMostProbes ||
            map.leaves()[map.leafAt(point).value].contents != nx::assets::Contents::Empty) {
            return;
        }
        const bool isFarFromTheOthers = std::ranges::none_of(positions, [point](nx::Vec3 other) {
            return nx::lengthSquared(other - point) < kProbeSpacing * kProbeSpacing;
        });
        if (isFarFromTheOthers) {
            positions.push_back(point);
        }
    };
    const auto middleOfModel = [&map](std::string_view model) -> std::optional<nx::Vec3> {
        if (model.size() < 2 || model.front() != '*') {
            return std::nullopt;
        }
        const std::optional<int> index = nx::assets::integerOfValue(model.substr(1));
        if (!index || *index <= 0 || static_cast<std::size_t>(*index) >= map.models().size()) {
            return std::nullopt;
        }
        const nx::Aabb& bounds = map.models()[static_cast<std::size_t>(*index)].bounds;
        return (bounds.min + bounds.max) * 0.5F;
    };
    for (const std::string_view spawn : {"info_player_start", "info_player_deathmatch"}) {
        for (const nx::assets::BspEntity& entity : map.entities()) {
            if (entity.find("classname") == spawn) {
                if (const std::optional<nx::Vec3> origin = entity.findVector("origin")) {
                    add(*origin + nx::Vec3{.z = kEyeHeight});
                }
            }
        }
    }
    for (const nx::assets::BspEntity& entity : map.entities()) {
        const std::optional<std::string_view> classname = entity.find("classname");
        if (classname == "func_bomb_target" || classname == "func_buyzone" ||
            classname == "func_hostage_rescue") {
            if (const std::optional<std::string_view> model = entity.find("model")) {
                if (const std::optional<nx::Vec3> middle = middleOfModel(*model)) {
                    add(*middle);
                }
            }
        } else if (classname == "info_bomb_target" || classname == "info_hostage_rescue") {
            if (const std::optional<nx::Vec3> origin = entity.findVector("origin")) {
                add(*origin + nx::Vec3{.z = kEyeHeight});
            }
        }
    }
    for (const nx::assets::BspLeaf& leaf : map.leaves()) {
        const nx::Vec3 size = leaf.bounds.max - leaf.bounds.min;
        if (leaf.contents == nx::assets::Contents::Empty && size.x >= kLeastLeafSide &&
            size.y >= kLeastLeafSide && size.z >= kLeastLeafSide) {
            add((leaf.bounds.min + leaf.bounds.max) * 0.5F);
        }
    }
    return positions;
}

auto nearestLights(std::span<const nx::render::StudioLight> lights, nx::Vec3 point, std::size_t count)
    -> std::vector<nx::render::StudioLight> {
    if (count == 0) {
        return {};
    }
    std::vector<nx::render::StudioLight> nearest(std::min(count, lights.size()));
    auto reaching = lights | std::views::filter([point](const nx::render::StudioLight& light) {
                        return nx::lengthSquared(light.position - point) < light.reach * light.reach;
                    });
    const auto distance = [point](const nx::render::StudioLight& light) {
        return nx::lengthSquared(light.position - point);
    };
    const auto selected = std::ranges::partial_sort_copy(reaching, nearest, {}, distance, distance);
    nearest.erase(selected.out, nearest.end());
    return nearest;
}

auto skyLightOf(const nx::assets::BspView& map) -> nx::render::SkyLight {
    nx::render::SkyLight sky;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        if (entity.find("classname") != "light_environment") {
            continue;
        }
        // Its keys in the order the map gives them: the engine reads `angles` and `angle`
        // (ED_ParseEdict), the game `pitch` and `_light`.
        nx::Vec3 angles{};
        for (const auto& [key, value] : entity.keyValues) {
            if (const std::optional<nx::Vec3> turned = nx::game::anglesAfter(angles, key, value)) {
                angles = *turned;
            } else if (key == "pitch") {
                angles.x = nx::assets::numberOfValue(value).value_or(0.0F);
            } else if (key == "_light") {
                sky.color = skyColorOf(value);
            }
        }
        // UTIL_MakeAimVectors: an aim's pitch is up.
        sky.direction = nx::angleVectors({.pitch = nx::Degrees{-angles.x},
                                          .yaw = nx::Degrees{angles.y},
                                          .roll = nx::Degrees{angles.z}})
                            .forward;
    }
    return sky;
}

} // namespace cs
