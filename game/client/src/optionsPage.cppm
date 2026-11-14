// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The options of the main menu (docs/12-ui-hud-and-console.md, section 4): the player's settings by what
/// they set, each on a row with what changes it, a slider, a switch, a choice among a few values or the
/// keys of an action, changed by the keys or the mouse and set as it is changed. Here is the page's state
/// and what it shows (OptionsFrame), its texts named by the fields of OptionsTexts; what a setting reads and
/// sets is given by whoever runs the menu, and drawing it is apart (cs.client:mainMenuPanel), in the
/// player's language as it is when it draws.
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
    Keyboard,
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

/// The keys of an action: their names as they are now, none for an action no key runs; what gives the
/// action the key an event presses, a mouse button and the wheel among them, and tells whether the event
/// pressed one; and what takes every key from it.
struct OptionKeys {
    std::function<std::string()> keys;
    std::function<bool(const nx::platform::WindowEvent&)> bind;
    std::function<void()> clear;
};

/// How a value of a choice is written: by a text of the options', in the player's language, or as it is
/// (a language's tag).
using ChoiceText = std::variant<std::string OptionsTexts::*, std::string>;

/// A setting that is one of a few values: how each is written, in their order, at least one; which of them
/// it is now, none while it is set to another (from the console); and what sets it to one of them, by its
/// place among them.
struct OptionChoice {
    std::vector<ChoiceText> values;
    std::function<std::optional<std::size_t>()> chosen;
    std::function<void(std::size_t)> choose;
};

/// What changes a setting.
using OptionControl = std::variant<OptionSlider, OptionSwitch, OptionKeys, OptionChoice>;

/// A setting the options list: the tab it is on, the text of the options' that names it, and what changes
/// it, which takes effect as it is changed.
struct OptionRow {
    OptionCategory category{};
    std::string OptionsTexts::* name = nullptr;
    OptionControl control;
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

/// The keys of an action as they show: their names, empty for none, and whether the page waits for the key
/// to give it.
struct ShownKeys {
    std::string keys{};
    bool isWaited = false;

    [[nodiscard]] inline auto operator==(const ShownKeys&) const noexcept -> bool = default;
};

/// A choice as it shows: how the value chosen is written; none while it is set to another.
struct ShownChoice {
    std::optional<ChoiceText> chosen{};

    [[nodiscard]] inline auto operator==(const ShownChoice&) const noexcept -> bool = default;
};

/// What changes a setting, as it shows.
using ShownControl = std::variant<ShownSlider, ShownSwitch, ShownKeys, ShownChoice>;

/// A row of the tab shown: the text that names its setting, and what changes it as it stands.
struct ShownOption {
    std::string OptionsTexts::* name = nullptr;
    ShownControl control;

    /// Defined in optionsPage.cpp: Clang 23 crashes generating it in a module that imports this one, where
    /// it would be made were it implicit (its control holds text since the keys of an action show).
    ~ShownOption();

    [[nodiscard]] inline auto operator==(const ShownOption&) const noexcept -> bool = default;
};

/// All the options show: the tab shown; its rows on those the page's list shows, from the one at `first` of
/// all `count`; and the row of the one selected, when it is among them. None is selected while the tabs are.
struct OptionsFrame {
    OptionCategory category{};
    std::vector<ShownOption> rows{};
    std::optional<std::size_t> selectedRow{};
    std::size_t first = 0;
    std::size_t count = 0;

    [[nodiscard]] inline auto operator==(const OptionsFrame&) const noexcept -> bool = default;
};

/// The value of `slider` as the options write it.
[[nodiscard]] auto sliderText(const ShownSlider& slider) -> std::string;

/// What the page asks of the menu it is on.
enum class OptionsOutcome : std::uint8_t {
    Stays,
    GoesBack, ///< To the menu's items.
};

/// The options' state, as the keys and the mouse change it: the tab shown, the setting selected and how far
/// the list is scrolled, the slider the mouse holds, and the action that waits for a key.
class OptionsPage {
public:
    /// The options of `listed`, in the order each tab lists its own.
    explicit OptionsPage(std::vector<OptionRow> listed) noexcept;

    /// Shows the first tab, the tabs selected.
    void open() noexcept;

    /// Whether an action waits for its key: every key and button is then the page's, the one that opens
    /// the console among them.
    [[nodiscard]] inline auto isWaitingForKey() const noexcept -> bool { return waitingRow.has_value(); }

    /// What `event` does to the options, laid out by `layout`, with the mouse's cursor at `cursor`. Left and
    /// Right change the tab while the tabs are selected, else the setting selected: a slider by a step, a
    /// switch over, a choice to the value before or after it, going around past either end. Up and Down
    /// select the setting before and the one after, the tabs above the first, the list following it; the
    /// wheel scrolls the list. Enter switches a switch over, takes a choice to its next value, and has an
    /// action wait for its key; Delete and Backspace take an action's keys from it. The mouse selects the row
    /// it is over; its left button shows the tab it is pressed on, switches a switch over, takes a choice to
    /// its next value, has an action wait for its key, and sets a slider to where it is pressed on its track,
    /// and on as it moves until it comes up. While an action waits, the
    /// next key, button or notch of the wheel is given to it, and Escape leaves it as it was. Else Escape,
    /// and the page's button, go back to the menu's items.
    [[nodiscard]] auto handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition cursor,
                              const MainMenuLayout& layout) -> OptionsOutcome;

    /// What it shows on a list of `listRows` rows.
    [[nodiscard]] auto frame(std::size_t listRows) const -> OptionsFrame;

private:
    /// The settings of the tab shown, from the top.
    [[nodiscard]] auto shownRows() const -> std::vector<const OptionRow*>;
    /// The setting on the first row of a list of `listRows` rows: as far as it was scrolled, or as far as
    /// such a list goes.
    [[nodiscard]] auto firstShownOf(std::size_t listRows) const -> std::size_t;
    /// Selects the setting at `index` of the tab's, and scrolls the list of `listRows` rows so that it
    /// shows.
    void select(std::size_t index, std::size_t listRows) noexcept;
    /// Shows the tab `tab`, its tabs selected.
    void show(OptionCategory tab) noexcept;
    /// Changes the setting selected by a step: a slider or a choice down or up as `steps` says, a switch
    /// over.
    void change(std::int32_t steps);
    /// What Enter, or the left button, does to the setting selected: a switch over, a choice to its next
    /// value, an action waits.
    void activate();
    /// Sets `slider` to where the cursor's `x` is on `track`.
    static void drag(const OptionSlider& slider, const nx::ui::PixelRect& track, float x);
    /// What `event` does while an action waits for its key.
    void giveKey(const nx::platform::WindowEvent& event);
    [[nodiscard]] auto press(const nx::platform::KeyChanged& key, std::size_t listRows) -> OptionsOutcome;
    [[nodiscard]] auto click(nx::platform::MousePosition cursor, const MainMenuLayout& layout)
        -> OptionsOutcome;

    std::vector<OptionRow> rows;
    OptionCategory category = OptionCategory::Mouse;
    std::optional<std::size_t> selected;   ///< Of the tab's settings; none while the tabs are selected.
    std::size_t firstShown = 0;            ///< The setting on the list's first row.
    std::optional<std::size_t> draggedRow; ///< The slider the mouse holds, by its setting.
    std::optional<std::size_t> waitingRow; ///< The action that waits for its key, by its setting.
};

} // namespace cs
