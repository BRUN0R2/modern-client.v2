// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;

namespace cs {

namespace {

/// The names hud.txt gives the HUD's pictures.
constexpr nx::EnumArray<HudPicture, std::string_view> kPictureNames{
    nx::EnumArray<HudPicture, std::string_view>::Storage{
        "number_0", "number_1", "number_2", "number_3", "number_4", "number_5", "number_6", "number_7",
        "number_8", "number_9", "cross", "suit_empty", "suit_full", "dollar", "stopwatch"}};
/// The screens the HUD's entries of hud.txt are for, the widths they start at.
constexpr std::uint32_t kResolution = 640;

/// The HUD's color, as it rests: (255, 160, 0) at an alpha of 100, added.
constexpr nx::render::ScreenColor kHudColor{.red = 255, .green = 160, .blue = 0, .alpha = 100};

// Where the elements are, in pixels from the screen's edges (docs/12-ui-hud-and-console.md, section
// 3.2: measured on the original at 640x480, 1280x720 and 1920x1080).
constexpr std::int32_t kCrossX = 10;
constexpr std::int32_t kHealthX = 36;          ///< Where the room of the health's digits starts.
constexpr std::int32_t kHealthAbove = 40;      ///< The cross's top and the health's, from the bottom.
constexpr std::int32_t kArmorAbove = 37;       ///< The suit's top and the armor's.
constexpr std::int32_t kSuitAcross = 5;        ///< The suit is a fifth of the way across.
constexpr std::int32_t kMoneyAbove = 75;       ///< The dollar's top and the money's.
constexpr std::int32_t kMoneyRightMargin = 16; ///< Between the money's room and the right edge.
constexpr std::int32_t kSuitAfterHealth = 32;  ///< Between the health's room and the suit at 640 wide.
/// The digits of each number the original has room for.
constexpr std::size_t kHealthRoom = 3;
constexpr std::size_t kArmorRoom = 3;
constexpr std::size_t kMoneyRoom = 5;
/// The armor that fills the suit.
constexpr float kFullArmor = 100.0F;
// The ammo (docs/12 section 3.2, measured with a Glock's 20 and 40).
constexpr std::int32_t kAmmoPictureFromRight = 42; ///< The left of the weapon's `ammo` picture.
constexpr std::int32_t kAmmoPictureAbove = 40;     ///< Its top, from the bottom.
constexpr std::int32_t kAmmoAbove = 37;            ///< The digits' and the bar's top.
constexpr std::int32_t kAmmoBarGap = 10;           ///< Between the bar and each number's room.
constexpr std::int32_t kAmmoBarWidth = 2;
constexpr std::size_t kAmmoRoom = 3;
// The round's time (docs/12 section 3.2, measured at 640x480, 1024x768 and 1280x720).
constexpr std::int32_t kTimerAbove = 37;       ///< The stopwatch's top and the digits'.
constexpr std::size_t kMinutesRoom = 2;        ///< Its room ends in the middle.
constexpr std::int32_t kSecondsFromMiddle = 6; ///< Where the seconds' digits start.
constexpr std::int32_t kColonFromMiddle = 2;   ///< The colon's left.
constexpr std::int32_t kColonTopAbove = 28;    ///< Its upper square's top, from the bottom.
constexpr std::int32_t kColonBottomAbove = 22; ///< Its lower square's.
constexpr float kColonSide = 2.0F;
constexpr std::uint32_t kSecondsPerMinute = 60;
/// The HUD's color, as the colon of the round's time is added in it.
constexpr nx::render::ScreenColor kColonColor{
    .red = kHudColor.red, .green = kHudColor.green, .blue = kHudColor.blue, .alpha = kTimerColonAlpha};

// The crosshair (docs/12 section 3.2, measured at 640x480 to 1600x900): its lines' length before its
// scale, the screen width its scale is 1 at, and the widths auto turns medium and large at.
constexpr std::uint64_t kCrosshairLength = 5;
constexpr std::uint64_t kCrosshairWidth = 640;
constexpr std::uint32_t kMediumFrom = 800;
constexpr std::uint32_t kLargeFrom = 1024;
/// The names cl_crosshair_size takes.
constexpr nx::EnumArray<CrosshairSize, std::string_view> kSizeNames{
    nx::EnumArray<CrosshairSize, std::string_view>::Storage{"auto", "small", "medium", "large"}};

/// A factor of the crosshair's scale, kept exact.
struct Fraction {
    std::uint64_t numerator = 1;
    std::uint64_t denominator = 1;
};

/// The factors of the small and the medium crosshair (docs/12 section 3.2): 0.625 and 0.8 of the large.
constexpr Fraction kSmallFactor{.numerator = 5, .denominator = 8};
constexpr Fraction kMediumFactor{.numerator = 4, .denominator = 5};

/// The factor of `size`, which is not Auto.
[[nodiscard]] auto factorOf(CrosshairSize size) noexcept -> Fraction {
    switch (size) {
    case CrosshairSize::Small:
        return kSmallFactor;
    case CrosshairSize::Medium:
        return kMediumFactor;
    default:
        return {};
    }
}

[[nodiscard]] auto isCrosshairSize(std::string_view text) noexcept -> bool {
    return crosshairSizeNamed(text).has_value();
}

[[nodiscard]] auto isCrosshairColor(std::string_view text) noexcept -> bool {
    return crosshairColorOf(text).has_value();
}

/// The digits of the whole part of `value`, a finite number above 0: exactly, however many.
[[nodiscard]] auto wholeDigits(float value) -> std::string {
    return std::format("{:.0f}", std::trunc(value));
}

/// The picture of the digit `digit`.
[[nodiscard]] auto digitPicture(char digit) noexcept -> HudPicture {
    nx::expects(digit >= '0' && digit <= '9', "digitPicture: a decimal digit");
    return static_cast<HudPicture>(std::to_underlying(HudPicture::Number0) + (digit - '0'));
}

[[nodiscard]] auto sizeOf(std::uint32_t pixels) noexcept -> std::int32_t {
    return nx::narrow<std::int32_t>(pixels);
}

} // namespace

auto hudSheetOf(std::span<const nx::assets::SpriteListEntry> entries) -> nx::Result<HudSheet, HudError> {
    HudSheet sheet;
    for (const HudPicture picture : HudSheet::keys()) {
        const auto found = std::ranges::find_if(entries, [picture](const nx::assets::SpriteListEntry& entry) {
            return entry.resolution == kResolution && entry.name == kPictureNames[picture];
        });
        if (found == entries.end()) {
            return nx::fail(HudError::PictureMissing, std::string{kPictureNames[picture]});
        }
        sheet[picture] = *found;
    }
    return sheet;
}

auto healthDigits(nx::HitPoints health) -> std::string {
    const float value = health.value();
    nx::expects(std::isfinite(value), "healthDigits: a finite health");
    if (!(value > 0.0F)) {
        return "0";
    }
    return value <= 1.0F ? "1" : wholeDigits(std::min(value, std::numeric_limits<float>::max()));
}

auto armorDigits(nx::ArmorPoints armor) -> std::string {
    const float value = armor.value();
    nx::expects(std::isfinite(value), "armorDigits: a finite armor");
    return value > 0.0F ? wholeDigits(std::min(value, std::numeric_limits<float>::max())) : "0";
}

auto moneyDigits(nx::Money money) -> std::string {
    return std::to_string(std::max(money.value(), 0));
}

auto layoutHud(const HudView& view, const HudSheet& sheet, nx::rhi::Extent2d size) -> std::vector<HudMark> {
    const std::int32_t width = sizeOf(size.width);
    const std::int32_t height = sizeOf(size.height);
    const std::int32_t digitWidth = sizeOf(sheet[HudPicture::Number0].rect.width);
    std::vector<HudMark> marks;
    // Each number's digits aligned to the right of its room, which more digits widen; returns where
    // the room ends.
    const auto addNumber = [&](std::string_view digits, std::int32_t x, std::int32_t y, std::size_t room) {
        const std::int32_t end = x + (nx::narrow<std::int32_t>(std::max(digits.size(), room)) * digitWidth);
        std::int32_t at = end - (nx::narrow<std::int32_t>(digits.size()) * digitWidth);
        for (const char digit : digits) {
            marks.push_back({.picture = digitPicture(digit), .x = at, .y = y});
            at += digitWidth;
        }
        return end;
    };

    const std::int32_t healthY = height - kHealthAbove;
    marks.push_back({.picture = HudPicture::Cross, .x = kCrossX, .y = healthY});
    const std::int32_t healthEnd = addNumber(healthDigits(view.health), kHealthX, healthY, kHealthRoom);

    const std::int32_t suitX = std::max(width / kSuitAcross, healthEnd + kSuitAfterHealth);
    const std::int32_t armorY = height - kArmorAbove;
    marks.push_back({.picture = HudPicture::SuitEmpty, .x = suitX, .y = armorY});
    const float armor = view.armor.value() > 0.0F ? std::min(view.armor.value(), kFullArmor) : 0.0F;
    const std::uint32_t suitHeight = sheet[HudPicture::SuitFull].rect.height;
    const auto filled = static_cast<std::uint32_t>(static_cast<float>(suitHeight) * armor / kFullArmor);
    if (filled > 0) {
        const std::uint32_t cut = suitHeight - filled;
        marks.push_back(
            {.picture = HudPicture::SuitFull, .x = suitX, .y = armorY + sizeOf(cut), .cutTop = cut});
    }
    std::ignore = addNumber(armorDigits(view.armor), suitX + sizeOf(sheet[HudPicture::SuitEmpty].rect.width),
                            armorY, kArmorRoom);

    const std::string money = moneyDigits(view.money);
    const std::int32_t moneyY = height - kMoneyAbove;
    const std::int32_t moneyEnd = width - kMoneyRightMargin;
    const std::int32_t moneyX =
        moneyEnd - (nx::narrow<std::int32_t>(std::max(money.size(), kMoneyRoom)) * digitWidth);
    marks.push_back({.picture = HudPicture::Dollar,
                     .x = moneyX - sizeOf(sheet[HudPicture::Dollar].rect.width),
                     .y = moneyY});
    std::ignore = addNumber(money, moneyX, moneyY, kMoneyRoom);
    return marks;
}

auto hudAmmoOf(const Inventory& inventory, const WeaponCatalog& catalog) -> std::optional<HudAmmo> {
    const OwnedWeapon* const held = inventory.held();
    if (held == nullptr) {
        return std::nullopt;
    }
    const std::optional<WeaponMagazine>& magazine = catalog.weapon(held->weapon).magazine;
    if (!magazine) {
        return std::nullopt;
    }
    return HudAmmo{.ammo = magazine->ammo, .clip = held->clip, .reserve = inventory.reserve(magazine->ammo)};
}

auto layoutAmmo(const HudAmmo& ammo, const HudSheet& sheet, nx::rhi::Extent2d size) -> AmmoLayout {
    const std::int32_t width = sizeOf(size.width);
    const std::int32_t height = sizeOf(size.height);
    const std::int32_t digitWidth = sizeOf(sheet[HudPicture::Number0].rect.width);
    const std::int32_t y = height - kAmmoAbove;
    AmmoLayout layout{.pictureX = width - kAmmoPictureFromRight, .pictureY = height - kAmmoPictureAbove};
    // A number's digits, added to `marks`, aligned to the right of its room, which ends at `end` and
    // more digits widen to the left; returns where the room starts.
    const auto addNumber = [&](std::vector<HudMark>& marks, std::uint32_t number, std::int32_t end) {
        const std::string digits = std::to_string(number);
        std::int32_t at = end - (nx::narrow<std::int32_t>(digits.size()) * digitWidth);
        for (const char digit : digits) {
            marks.push_back({.picture = digitPicture(digit), .x = at, .y = y});
            at += digitWidth;
        }
        return end - (nx::narrow<std::int32_t>(std::max(digits.size(), kAmmoRoom)) * digitWidth);
    };
    std::vector<HudMark> reserve;
    const std::int32_t reserveStart = addNumber(reserve, ammo.reserve, layout.pictureX);
    const std::int32_t barX = reserveStart - kAmmoBarGap - kAmmoBarWidth;
    layout.bar = nx::render::ScreenRect{.x = static_cast<float>(barX),
                                        .y = static_cast<float>(y),
                                        .width = static_cast<float>(kAmmoBarWidth),
                                        .height = static_cast<float>(sheet[HudPicture::Number0].rect.height)};
    std::ignore = addNumber(layout.digits, ammo.clip, barX - kAmmoBarGap);
    std::ranges::move(reserve, std::back_inserter(layout.digits));
    return layout;
}

auto layoutTimer(std::uint32_t seconds, const HudSheet& sheet, nx::rhi::Extent2d size) -> TimerLayout {
    const std::int32_t middle = sizeOf(size.width) / 2;
    const std::int32_t height = sizeOf(size.height);
    const std::int32_t y = height - kTimerAbove;
    const std::int32_t digitWidth = sizeOf(sheet[HudPicture::Number0].rect.width);
    TimerLayout layout;
    const auto addDigits = [&](std::string_view digits, std::int32_t x) {
        for (const char digit : digits) {
            layout.marks.push_back({.picture = digitPicture(digit), .x = x, .y = y});
            x += digitWidth;
        }
    };
    const std::string minutes = std::to_string(seconds / kSecondsPerMinute);
    const std::int32_t roomStart =
        middle - (nx::narrow<std::int32_t>(std::max(minutes.size(), kMinutesRoom)) * digitWidth);
    layout.marks.push_back({.picture = HudPicture::Stopwatch,
                            .x = roomStart - sizeOf(sheet[HudPicture::Stopwatch].rect.width),
                            .y = y});
    addDigits(minutes, middle - (nx::narrow<std::int32_t>(minutes.size()) * digitWidth));
    addDigits(std::format("{:02}", seconds % kSecondsPerMinute), middle + kSecondsFromMiddle);
    const auto square = [&](std::int32_t above) {
        return nx::render::ScreenRect{.x = static_cast<float>(middle + kColonFromMiddle),
                                      .y = static_cast<float>(height - above),
                                      .width = kColonSide,
                                      .height = kColonSide};
    };
    layout.colon = {square(kColonTopAbove), square(kColonBottomAbove)};
    return layout;
}

auto crosshairSizeNamed(std::string_view text) noexcept -> std::optional<CrosshairSize> {
    for (const CrosshairSize size : nx::EnumArray<CrosshairSize, std::string_view>::keys()) {
        if (kSizeNames[size] == text) {
            return size;
        }
    }
    return std::nullopt;
}

auto crosshairColorOf(std::string_view text) noexcept -> std::optional<nx::render::ScreenColor> {
    constexpr std::uint32_t kChannelTop = 255;
    std::array<std::uint8_t, 3> channels{};
    std::size_t count = 0;
    for (const auto part : std::views::split(text, ' ')) {
        const std::string_view word{part};
        if (word.empty()) {
            continue;
        }
        std::uint32_t channel = 0;
        const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), channel);
        if (count == channels.size() || error != std::errc{} || end != word.data() + word.size() ||
            channel > kChannelTop) {
            return std::nullopt;
        }
        channels[count++] = static_cast<std::uint8_t>(channel);
    }
    if (count != channels.size()) {
        return std::nullopt;
    }
    return nx::render::ScreenColor{.red = channels[0], .green = channels[1], .blue = channels[2]};
}

auto HudCvars::registerIn(nx::console::Console& console) -> nx::Result<HudCvars, nx::console::CvarError> {
    using nx::console::CvarDesc;
    constexpr nx::console::CvarFlags kArchived{nx::console::CvarFlag::Archive};
    auto color = console.registerCvar(CvarDesc<std::string>{
        .name = "cl_crosshair_color",
        .defaultValue = "50 250 50",
        .description = "The crosshair's color: red, green and blue, from 0 to 255.",
        .flags = kArchived,
        .rule = {.expectation = "three whole numbers from 0 to 255", .accepts = isCrosshairColor}});
    auto size = console.registerCvar(CvarDesc<std::string>{
        .name = "cl_crosshair_size",
        .defaultValue = "auto",
        .description = "The crosshair's size: auto picks it by the screen's width.",
        .flags = kArchived,
        .rule = {.expectation = "auto, small, medium or large", .accepts = isCrosshairSize}});
    auto translucent = console.registerCvar(
        nx::console::CvarDesc<bool>{.name = "cl_crosshair_translucent",
                                    .defaultValue = true,
                                    .description = "Whether the crosshair is added to what is behind it.",
                                    .flags = kArchived});
    auto rightHand = console.registerCvar(nx::console::CvarDesc<bool>{
        .name = "cl_righthand",
        .defaultValue = true,
        .description = "Whether the weapons are held in the right hand, else the left.",
        .flags = kArchived});
    if (!color || !size) {
        return std::unexpected{std::move(!color ? color.error() : size.error())};
    }
    for (auto* const registered : {&translucent, &rightHand}) {
        if (!*registered) {
            return std::unexpected{std::move(registered->error())};
        }
    }
    return HudCvars{.crosshairColor = *color,
                    .crosshairSize = *size,
                    .crosshairTranslucent = *translucent,
                    .rightHand = *rightHand};
}

auto HudCvars::hand() const -> Hand {
    return rightHand.value() ? Hand::Right : Hand::Left;
}

auto HudCvars::crosshair() const -> CrosshairStyle {
    const std::optional<nx::render::ScreenColor> color = crosshairColorOf(crosshairColor.value());
    const std::optional<CrosshairSize> size = crosshairSizeNamed(crosshairSize.value());
    nx::invariant(color.has_value() && size.has_value(), "HudCvars: their rules keep their values valid");
    const CrosshairStyle defaults{};
    return {.color = color.value_or(defaults.color),
            .size = size.value_or(defaults.size),
            .isTranslucent = crosshairTranslucent.value()};
}

auto crosshairRects(CrosshairSize crosshairSize, std::uint32_t distance, nx::rhi::Extent2d size)
    -> std::array<nx::render::ScreenRect, 4> {
    if (crosshairSize == CrosshairSize::Auto) {
        crosshairSize = size.width < kMediumFrom  ? CrosshairSize::Small
                        : size.width < kLargeFrom ? CrosshairSize::Medium
                                                  : CrosshairSize::Large;
    }
    const Fraction factor = factorOf(crosshairSize);
    // ⌊units · width / 640 · factor⌋, in whole numbers: exact at every width.
    const auto scaled = [&](std::uint64_t units) {
        return static_cast<float>((units * size.width * factor.numerator) /
                                  (kCrosshairWidth * factor.denominator));
    };
    const float length = scaled(kCrosshairLength);
    const float gap = scaled(distance);
    const auto x = static_cast<float>(size.width / 2);
    const auto y = static_cast<float>(size.height / 2);
    return {nx::render::ScreenRect{.x = x - gap - length + 1.0F, .y = y, .width = length, .height = 1.0F},
            nx::render::ScreenRect{.x = x + gap, .y = y, .width = length, .height = 1.0F},
            nx::render::ScreenRect{.x = x, .y = y - gap - length + 1.0F, .width = 1.0F, .height = length},
            nx::render::ScreenRect{.x = x, .y = y + gap, .width = 1.0F, .height = length}};
}

Hud::Hud(HudSheet sheet, std::vector<nx::render::ScreenImage> images,
         nx::EnumArray<HudPicture, std::size_t> imageOf, AmmoPictures ammoPictures) noexcept
    : m_sheet{std::move(sheet)}, m_images{std::move(images)}, m_imageOf{imageOf},
      m_ammoPictures{std::move(ammoPictures)} {}

auto Hud::load(const nx::assets::Vfs& vfs, std::span<const AmmoType> ammo, nx::render::ScreenRenderer& screen,
               nx::rhi::CommandList& commands) -> nx::Result<Hud, HudError> {
    const auto read = [&vfs](std::string_view file) {
        return nx::assets::VfsPath::parse(file).and_then(
            [&vfs](const nx::assets::VfsPath& path) { return vfs.readAll(path); });
    };
    auto listBytes = read("sprites/hud.txt");
    if (!listBytes) {
        return nx::fail(HudError::ListUnreadable, std::move(listBytes.error()));
    }
    const std::string text = *listBytes |
                             std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
                             std::ranges::to<std::string>();
    auto entries = nx::assets::parseSpriteList(text);
    if (!entries) {
        return nx::fail(HudError::ListUnreadable, "sprites/hud.txt", std::move(entries.error()));
    }
    auto sheet = hudSheetOf(*entries);
    if (!sheet) {
        return std::unexpected{std::move(sheet.error())};
    }
    // Each sprite once, by its path, in the order the pictures first name it.
    std::vector<std::string> sprites;
    const auto imageOfSprite = [&sprites](std::string_view sprite) {
        const auto found = std::ranges::find(sprites, sprite);
        if (found == sprites.end()) {
            sprites.emplace_back(sprite);
            return sprites.size() - 1;
        }
        return nx::narrow<std::size_t>(found - sprites.begin());
    };
    nx::EnumArray<HudPicture, std::size_t> imageOf;
    for (const HudPicture picture : HudSheet::keys()) {
        imageOf[picture] = imageOfSprite(std::format("sprites/{}.spr", (*sheet)[picture].sprite));
    }
    AmmoPictures ammoPictures;
    for (const AmmoType& kind : ammo) {
        const SpritePicture& picture = kind.picture;
        ammoPictures.emplace(
            kind.id,
            Picture{
                .rect = {.x = picture.x, .y = picture.y, .width = picture.width, .height = picture.height},
                .image = imageOfSprite(picture.sprite)});
    }
    std::vector<nx::render::ScreenImage> images;
    images.reserve(sprites.size());
    for (const std::string& file : sprites) {
        auto bytes = read(file);
        if (!bytes) {
            return nx::fail(HudError::SpriteUnreadable, file, std::move(bytes.error()));
        }
        auto view = nx::assets::SpriteView::parse(*bytes);
        if (!view) {
            return nx::fail(HudError::SpriteUnreadable, file, std::move(view.error()));
        }
        auto frames = screen.loadSprite(commands, *view, std::format("hud.{}", file));
        if (!frames) {
            return nx::fail(HudError::GpuFailed, file, std::move(frames.error()));
        }
        if (frames->empty()) {
            return nx::fail(HudError::SpriteUnreadable, std::format("{}: no frame", file));
        }
        images.push_back(std::move(frames->front()));
    }
    return Hud{std::move(*sheet), std::move(images), imageOf, std::move(ammoPictures)};
}

void Hud::append(std::vector<nx::render::ScreenQuad>& quads, const HudView& view,
                 const CrosshairStyle& crosshair, nx::rhi::Extent2d size) const {
    const auto addMark = [&](const HudMark& mark) {
        nx::assets::SpriteRect source = m_sheet[mark.picture].rect;
        source.y += mark.cutTop;
        source.height -= mark.cutTop;
        quads.push_back(nx::render::spriteQuad(m_images[m_imageOf[mark.picture]], source,
                                               static_cast<float>(mark.x), static_cast<float>(mark.y),
                                               kHudColor, nx::render::ScreenBlend::Additive));
    };
    std::ranges::for_each(layoutHud(view, m_sheet, size), addMark);
    if (view.roundTime) {
        const TimerLayout timer = layoutTimer(*view.roundTime, m_sheet, size);
        std::ranges::for_each(timer.marks, addMark);
        for (const nx::render::ScreenRect& square : timer.colon) {
            quads.push_back(nx::render::ScreenQuad{
                .target = square, .color = kColonColor, .blend = nx::render::ScreenBlend::Additive});
        }
    }
    if (view.ammo) {
        const AmmoLayout ammo = layoutAmmo(*view.ammo, m_sheet, size);
        std::ranges::for_each(ammo.digits, addMark);
        quads.push_back(nx::render::ScreenQuad{
            .target = ammo.bar, .color = kHudColor, .blend = nx::render::ScreenBlend::Additive});
        if (const auto found = m_ammoPictures.find(view.ammo->ammo); found != m_ammoPictures.end()) {
            const Picture& picture = found->second;
            quads.push_back(nx::render::spriteQuad(
                m_images[picture.image], picture.rect, static_cast<float>(ammo.pictureX),
                static_cast<float>(ammo.pictureY), kHudColor, nx::render::ScreenBlend::Additive));
        }
    }
    if (!view.crosshairDistance) {
        return;
    }
    // As FillRGBA draws it: added at its full alpha, or drawn as it is.
    const nx::render::ScreenBlend blend =
        crosshair.isTranslucent ? nx::render::ScreenBlend::Additive : nx::render::ScreenBlend::Normal;
    for (const nx::render::ScreenRect& line : crosshairRects(crosshair.size, *view.crosshairDistance, size)) {
        quads.push_back(nx::render::ScreenQuad{.target = line, .color = crosshair.color, .blend = blend});
    }
}

} // namespace cs
