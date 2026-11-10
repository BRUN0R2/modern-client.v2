// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The options of the main menu (docs/12-ui-hud-and-console.md, section 4): the player's settings by what
/// they set, each on a row with what changes it, a slider or a switch, changed by the keys or the mouse and
/// set as it is changed. Here is the page's state and what it shows (OptionsFrame); what a setting reads and
/// sets is given by whoever runs the menu, and drawing it is apart (cs.client:mainMenuPanel).
export module cs.client:optionsPage;

import std;
import nx.core;
import nx.platform;
import nx.ui;
import cs.shared;
import :mainMenuLayout;

export namespace cs {

/// What the options set, each on a tab of its own.
enum class OptionCategory : std::uint8_t {
    Mouse,
    Audio,
    Video,
    Game,
    Count,
};

/// How a slider's value is written.
enum class OptionValueStyle : std::uint8_t {
    Tenths,  ///< With a decimal: "3.0".
    Whole,   ///< As a whole number: "100".
    Percent, ///< As a share of one, in whole percents: "70%".
};

/// A setting of a value from `minimum` to `maximum`, changed by `step` at a time: what it is now, what sets
/// it to a value of that range, and how it is written.
struct OptionSlider {
    std::function<float()> value;
    std::function<void(float)> set;
    float minimum = 0.0F;
    float maximum = 1.0F;
    float step = 1.0F;
    OptionValueStyle style = OptionValueStyle::Tenths;
};

/// A setting that is on or off: whether it is on now, and what turns it on or off.
struct OptionSwitch {
    std::function<bool()> isOn;
    std::function<void(bool)> set;
};

/// A setting the options list: the tab it is on, the text of the options' that names it, and what changes
/// it.
struct OptionRow {
    OptionCategory category{};
    std::string OptionsTexts::* name = nullptr;
    std::variant<OptionSlider, OptionSwitch> control;
};

/// A slider as it shows: its value, how it is written, and the share of its track the value is at, from 0
/// to 1.
struct ShownSlider {
    float value = 0.0F;
    OptionValueStyle style = OptionValueStyle::Tenths;
    float share = 0.0F;

    [[nodiscard]] inline auto operator==(const ShownSlider&) const noexcept -> bool = default;
};

/// A switch as it shows.
struct ShownSwitch {
    bool isOn = false;

    [[nodiscard]] inline auto operator==(const ShownSwitch&) const noexcept -> bool = default;
};

/// A row of the tab shown: the text that names its setting, and what changes it as it stands.
struct ShownOption {
    std::string OptionsTexts::* name = nullptr;
    std::variant<ShownSlider, ShownSwitch> control;

    [[nodiscard]] inline auto operator==(const ShownOption&) const noexcept -> bool = default;
};

/// All the options show: the tab shown, its rows from the top, and the row selected; none while the tabs
/// are.
struct OptionsFrame {
    OptionCategory category{};
    std::vector<ShownOption> rows{};
    std::optional<std::size_t> selectedRow{};

    [[nodiscard]] inline auto operator==(const OptionsFrame&) const noexcept -> bool = default;
};

/// The value of `slider` as the options write it.
[[nodiscard]] auto sliderText(const ShownSlider& slider) -> std::string;

/// What the page asks of the menu it is on.
enum class OptionsOutcome : std::uint8_t {
    Stays,
    GoesBack, ///< To the menu's items.
};

/// The options' state, as the keys and the mouse change it: the tab shown, the row selected, and the slider
/// the mouse holds.
class OptionsPage {
public:
    /// The options of `listed`, in the order each tab lists its own.
    explicit OptionsPage(std::vector<OptionRow> listed) noexcept;

    /// Shows the first tab, the tabs selected.
    void open() noexcept;

    /// What `event` does to the options, laid out by `layout`, with the mouse's cursor at `cursor`. Left and
    /// Right change the tab while the tabs are selected, else the row selected: a slider by a step, a switch
    /// over. Up and Down select the row before and the one after, the tabs above the first. Enter switches a
    /// switch over. The mouse selects the row it is over; its left button shows the tab it is pressed on,
    /// switches a switch over, and sets a slider to where it is pressed on its track, and on as it moves
    /// until it comes up. Escape, and the page's button, go back to the menu's items.
    [[nodiscard]] auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                              const MainMenuLayout& layout) -> OptionsOutcome;

    /// What it shows.
    [[nodiscard]] auto frame() const -> OptionsFrame;

private:
    /// The settings of the tab shown, from the top.
    [[nodiscard]] auto shownRows() const -> std::vector<const OptionRow*>;
    /// Changes the row selected by a step: a slider down or up as `steps` says, a switch over.
    void change(std::int32_t steps);
    /// Sets `slider` to where the cursor's `x` is on `track`.
    static void drag(const OptionSlider& slider, const nx::ui::PixelRect& track, float x);
    [[nodiscard]] auto press(const nx::platform::KeyChanged& key) -> OptionsOutcome;
    [[nodiscard]] auto click(nx::platform::MousePosition cursor, const MainMenuLayout& layout)
        -> OptionsOutcome;

    std::vector<OptionRow> rows;
    OptionCategory category = OptionCategory::Mouse;
    std::optional<std::size_t> selectedRow;
    std::optional<std::size_t> draggedRow; ///< The slider the mouse holds, by its row.
};

} // namespace cs
