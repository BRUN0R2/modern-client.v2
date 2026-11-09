// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.platform;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import cs.client;

namespace app {

namespace {

constexpr nx::log::Category kLog{"client"};

} // namespace

ClientBuyMenu::ClientBuyMenu(const cs::GameContent& content, PlayedSession played,
                             const nx::platform::Stopwatch& mapClock)
    : played{std::move(played)}, mapClock{&mapClock}, content{.weapons = &content.weapons,
                                                              .armorPrices = &content.buying.armor,
                                                              .look = &content.buyMenu,
                                                              .armorRatio = content.playerHarm.armor.ratio},
      menu{this->content, buyActions()}, panel{std::in_place, this->content} {}

auto ClientBuyMenu::buyActions() -> cs::BuyActions {
    return cs::BuyActions{
        .weapon = [this](cs::WeaponIndex weapon) -> nx::Result<void, cs::BuyRefusal> {
            Session* const session = played();
            if (session == nullptr) {
                return nx::fail(cs::BuyRefusal::NoMap);
            }
            return session->game->buyWeapon(session->player, weapon);
        },
        .armor = [this](cs::ArmorKind kind) -> nx::Result<void, cs::BuyRefusal> {
            Session* const session = played();
            if (session == nullptr) {
                return nx::fail(cs::BuyRefusal::NoMap);
            }
            return session->game->buyArmor(session->player, kind);
        },
        .ammo = [this](cs::WeaponSlot slot, cs::AmmoPurchase purchase) -> nx::Result<void, cs::BuyRefusal> {
            Session* const session = played();
            if (session == nullptr) {
                return nx::fail(cs::BuyRefusal::NoMap);
            }
            return session->game->buyAmmo(session->player, slot, purchase);
        },
        .rebuy = [this]() -> nx::Result<void, cs::BuyRefusal> {
            Session* const session = played();
            if (session == nullptr) {
                return nx::fail(cs::BuyRefusal::NoMap);
            }
            return session->game->rebuy(session->player);
        },
        .refused = [this](cs::BuyRefusal refusal) { tellRefusal(refusal); }};
}

auto ClientBuyMenu::registerCommands(nx::console::Console& console, const cs::WeaponCatalog& weapons)
    -> nx::Result<void, ClientError> {
    if (auto registered = cs::registerBuyCommands(console, weapons, buyActions()); !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    cs::BuyMenuCommands commands{.toggle = [this]() -> nx::Result<void, cs::BuyRefusal> {
                                     return showable().and_then([this](Session* session) {
                                         return menu.toggle(buyerViewOf(*session));
                                     });
                                 },
                                 .openEquipment = [this]() -> nx::Result<void, cs::BuyRefusal> {
                                     return showable().and_then([this](Session* session) {
                                         return menu.openEquipment(buyerViewOf(*session));
                                     });
                                 },
                                 .pressSlot =
                                     [this](std::uint32_t slot) {
                                         if (const Session* session = played(); session != nullptr) {
                                             menu.pressSlot(slot, buyerViewOf(*session));
                                         }
                                     },
                                 .cancel = [this] { menu.back(); },
                                 .refused = [this](cs::BuyRefusal refusal) { tellRefusal(refusal); }};
    if (auto registered = cs::registerBuyMenuCommands(console, std::move(commands)); !registered) {
        return nx::fail(ClientError::SettingsUnavailable, std::move(registered.error()));
    }
    return {};
}

auto ClientBuyMenu::isOpen() const noexcept -> bool {
    return menu.isOpen();
}

auto ClientBuyMenu::isShown() const noexcept -> bool {
    return played() != nullptr && panel && menu.isOpen();
}

void ClientBuyMenu::update() {
    if (const Session* session = played(); session != nullptr) {
        menu.update(buyerViewOf(*session)); // what the player may no longer buy it stops offering
    }
}

auto ClientBuyMenu::handle(const nx::platform::WindowEvent& event, nx::platform::MousePosition mouse,
                           nx::rhi::Extent2d size) -> bool {
    const Session* session = played();
    return session != nullptr && menu.isOpen() && menu.handle(event, mouse, size, buyerViewOf(*session));
}

void ClientBuyMenu::reset() {
    menu.close();
    if (panel) {
        panel->clearNotice();
    }
}

auto ClientBuyMenu::needsLoading() const noexcept -> bool {
    return panel && (!panel->isLoaded() || menu.isOpen());
}

void ClientBuyMenu::load(nx::ui::FontCatalog& fonts, const nx::assets::Vfs& vfs,
                         nx::render::ScreenRenderer& screen, nx::rhi::CommandList& list,
                         std::uint32_t screenHeight) {
    if (auto loaded = panel->load(fonts, vfs, screen, list, screenHeight); !loaded) {
        nx::log::warning(kLog, "the buy menu cannot be shown: {}", nx::describe(loaded.error()));
        panel.reset(); // the game plays on, as without the HUD
        menu.close();
    }
}

void ClientBuyMenu::appendBackdrop(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) const {
    if (isShown()) {
        cs::BuyMenuPanel::appendBackdrop(quads, size);
    }
}

void ClientBuyMenu::append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) {
    if (!isShown()) {
        return;
    }
    const cs::BuyerView view = buyerViewOf(*played());
    panel->append(quads, menu.frameOf(view), cs::buyTimeShareOf(view.buying), size);
}

void ClientBuyMenu::appendNotice(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) {
    if (played() != nullptr && panel) {
        panel->appendNotice(quads, size, now(), isShown());
    }
}

auto ClientBuyMenu::buyerViewOf(const Session& session) -> cs::BuyerView {
    const cs::ClientGame& client = session.client;
    return {.team = client.team(),
            .status = client.status(),
            .inventory = &client.inventory(),
            .buying = client.buying(),
            .lastPurchase = client.lastPurchase()};
}

auto ClientBuyMenu::showable() const -> nx::Result<Session*, cs::BuyRefusal> {
    Session* const session = played();
    if (session == nullptr) {
        return nx::fail(cs::BuyRefusal::NoMap);
    }
    if (!panel) {
        return nx::fail(cs::BuyRefusal::NoMenu);
    }
    return session;
}

void ClientBuyMenu::tellRefusal(cs::BuyRefusal refusal) {
    if (panel) {
        panel->showNotice(cs::buyRefusalText(refusal), now());
    }
}

auto ClientBuyMenu::now() const -> nx::Seconds {
    return nx::Seconds{mapClock->elapsed()};
}

} // namespace app
