// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.assets;

namespace nx::ui {

namespace {

/// `word` as a whole number; none for anything else.
[[nodiscard]] auto wholeNumberOf(std::string_view word) noexcept -> std::optional<std::int32_t> {
    std::int32_t value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    return error == std::errc{} && end == word.data() + word.size() && !word.empty() ? std::optional{value}
                                                                                     : std::nullopt;
}

/// The setting `key` of `settings` as a whole number, `current` when it is left out.
[[nodiscard]] auto numberSetting(const assets::KeyValuesEntry& settings, std::string_view key,
                                 std::int32_t current) -> Result<std::int32_t, PanelError> {
    const std::optional<std::string_view> word = settings.findWord(key);
    if (!word) {
        return current;
    }
    const std::optional<std::int32_t> number = wholeNumberOf(*word);
    if (!number) {
        return fail(PanelError::BadSetting, std::format("{:?} {}: {:?}", settings.key, key, *word));
    }
    return *number;
}

/// A position setting: from the start of the parent's `extent`, from its end with `r`, from its middle
/// with `c`; `current` when it is left out.
[[nodiscard]] auto positionSetting(const assets::KeyValuesEntry& settings, std::string_view key,
                                   std::int32_t extent, std::int32_t current)
    -> Result<std::int32_t, PanelError> {
    const std::optional<std::string_view> word = settings.findWord(key);
    if (!word) {
        return current;
    }
    const char anchor = word->empty() ? '\0' : toLowerAscii(word->front());
    const bool isAnchored = anchor == 'r' || anchor == 'c';
    const std::optional<std::int32_t> number = wholeNumberOf(isAnchored ? word->substr(1) : *word);
    if (!number) {
        return fail(PanelError::BadSetting, std::format("{:?} {}: {:?}", settings.key, key, *word));
    }
    return anchor == 'r' ? extent - *number : anchor == 'c' ? (extent / 2) + *number : *number;
}

/// An enumeration setting, its value the enumerator's number; `current` when it is left out.
template <class Enum>
[[nodiscard]] auto enumSetting(const assets::KeyValuesEntry& settings, std::string_view key, Enum current)
    -> Result<Enum, PanelError> {
    auto number = numberSetting(settings, key, static_cast<std::int32_t>(std::to_underlying(current)));
    if (!number) {
        return std::unexpected{std::move(number.error())};
    }
    if (*number < 0 || *number >= static_cast<std::int32_t>(std::to_underlying(Enum::Count))) {
        return fail(PanelError::BadSetting, std::format("{:?} {}: {}", settings.key, key, *number));
    }
    return static_cast<Enum>(*number);
}

} // namespace

Panel::Panel(std::string name, PanelBounds bounds) : m_name{std::move(name)}, m_bounds{bounds} {}

auto Panel::add(std::unique_ptr<Panel> child) -> Panel& {
    expects(child != nullptr, "Panel::add: a panel");
    child->m_parent = this;
    return *m_children.emplace_back(std::move(child));
}

auto Panel::find(std::string_view name) const noexcept -> Panel* {
    const auto found = std::ranges::find_if(m_children, [name](const std::unique_ptr<Panel>& child) {
        return equalsIgnoringCase(child->m_name, name);
    });
    return found == m_children.end() ? nullptr : found->get();
}

void Panel::setBounds(PanelBounds bounds) {
    const std::int32_t wider = bounds.wide - m_bounds.wide;
    const std::int32_t taller = bounds.tall - m_bounds.tall;
    m_bounds = bounds;
    if (wider == 0 && taller == 0) {
        return;
    }
    for (const std::unique_ptr<Panel>& child : m_children) {
        child->followParent(wider, taller);
    }
}

void Panel::followParent(std::int32_t wider, std::int32_t taller) {
    // Each edge follows the parent's left or right, top or bottom: the left and top edges the pinned
    // corner's, the right and bottom ones the parent's right and bottom when the panel resizes that
    // way, else the left and top edges', keeping the panel's size.
    const bool leftFollowsRight = m_pinCorner == PinCorner::TopRight || m_pinCorner == PinCorner::BottomRight;
    const bool topFollowsBottom =
        m_pinCorner == PinCorner::BottomLeft || m_pinCorner == PinCorner::BottomRight;
    const bool rightFollowsRight =
        m_autoResize == AutoResize::Right || m_autoResize == AutoResize::DownAndRight || leftFollowsRight;
    const bool bottomFollowsBottom =
        m_autoResize == AutoResize::Down || m_autoResize == AutoResize::DownAndRight || topFollowsBottom;
    const std::int32_t left = leftFollowsRight ? wider : 0;
    const std::int32_t top = topFollowsBottom ? taller : 0;
    const std::int32_t right = rightFollowsRight ? wider : 0;
    const std::int32_t bottom = bottomFollowsBottom ? taller : 0;
    setBounds({.x = m_bounds.x + left,
               .y = m_bounds.y + top,
               .wide = m_bounds.wide + right - left,
               .tall = m_bounds.tall + bottom - top});
}

auto Panel::applySettings(const assets::KeyValuesEntry& settings) -> Result<void, PanelError> {
    const PanelBounds parentBounds = m_parent != nullptr ? m_parent->m_bounds : PanelBounds{};
    // Read whole before any of it is applied: a setting that cannot be read changes nothing.
    PanelBounds bounds = m_bounds;
    std::array numbers{std::pair{"visible", m_isVisible ? 1 : 0}, std::pair{"enabled", m_isEnabled ? 1 : 0},
                       std::pair{"tabPosition", m_tabPosition}};
    for (auto& [key, value] : numbers) {
        auto read = numberSetting(settings, key, value);
        if (!read) {
            return std::unexpected{std::move(read.error())};
        }
        value = *read;
    }
    for (const auto& [key, extent, value] : {std::tuple{"xpos", parentBounds.wide, &bounds.x},
                                             std::tuple{"ypos", parentBounds.tall, &bounds.y}}) {
        auto read = positionSetting(settings, key, extent, *value);
        if (!read) {
            return std::unexpected{std::move(read.error())};
        }
        *value = *read;
    }
    for (const auto& [key, value] : {std::pair{"wide", &bounds.wide}, std::pair{"tall", &bounds.tall}}) {
        auto read = numberSetting(settings, key, *value);
        if (!read) {
            return std::unexpected{std::move(read.error())};
        }
        *value = *read;
    }
    auto pin = enumSetting(settings, "pinCorner", m_pinCorner);
    auto resize = enumSetting(settings, "autoResize", m_autoResize);
    if (!pin || !resize) {
        return std::unexpected{std::move(!pin ? pin.error() : resize.error())};
    }
    setBounds(bounds);
    m_isVisible = numbers[0].second != 0;
    m_isEnabled = numbers[1].second != 0;
    m_tabPosition = numbers[2].second;
    m_pinCorner = *pin;
    m_autoResize = *resize;
    return {};
}

auto Panel::applyLayout(const assets::KeyValuesEntry& layout) -> Result<void, PanelError> {
    for (const assets::KeyValuesEntry& entry : layout.entries) {
        Panel* child = entry.isBlock ? find(entry.key) : nullptr;
        if (child == nullptr) {
            continue;
        }
        if (auto applied = child->applySettings(entry); !applied) {
            return applied;
        }
    }
    return {};
}

} // namespace nx::ui
