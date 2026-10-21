// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

namespace nx::game {

namespace {

/// Button `entity` shows its alternate textures, or its usual ones (pev->frame 1 or 0).
void showAlternateTextures(ecs::World& world, ecs::Entity entity, bool isShown) {
    if (AlternateTextures* textures = world.get<AlternateTextures>(entity)) {
        textures->isShown = isShown;
    }
}

/// Button `entity` starts going in (CBaseButton::ButtonActivate).
void pressButton(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    Button& button = *world.get<Button>(entity);
    button.state = ButtonState::GoingIn;
    moveTo(world, hulls, entity, button.in, button.speed);
}

} // namespace

void useButton(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
               ecs::Entity activator) {
    Button& button = *world.get<Button>(entity);
    if (button.isTouchOnly || button.state == ButtonState::GoingIn || button.state == ButtonState::GoingOut) {
        return;
    }
    button.activator = activator;
    if (button.state == ButtonState::Out) {
        pressButton(world, hulls, entity);
    } else if (!button.staysIn && button.isToggle) {
        returnButton(world, hulls, entity); // Parity: the original fires no targets here.
    }
}

void touchButton(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity,
                 ecs::Entity toucher) {
    Button& button = *world.get<Button>(entity);
    if (!button.isTouchable) {
        return;
    }
    // CBaseButton::ButtonResponseToTouch: an out button goes in; an in toggle comes back out.
    const bool isComingOut = button.state == ButtonState::In && button.isToggle && !button.staysIn;
    if (button.state != ButtonState::Out && !isComingOut) {
        return;
    }
    button.activator = toucher;
    button.isTouchable = false; // until its move is done
    if (isComingOut) {
        useTargets(world, hulls, entity, toucher);
        if (world.isAlive(entity)) {
            returnButton(world, hulls, entity);
        }
        return;
    }
    pressButton(world, hulls, entity);
}

void buttonArrived(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    Button& button = *world.get<Button>(entity);
    if (button.state == ButtonState::GoingIn) {
        button.state = ButtonState::In;
        showAlternateTextures(world, entity, true);
        if (button.staysIn || button.isToggle) {
            button.isTouchable = button.isTouchOnly;
        } else {
            Pusher& pusher = *world.get<Pusher>(entity);
            pusher.nextThink = pusher.localTime.after(button.wait, tickTimeOf(world).rate);
        }
        useTargets(world, hulls, entity, button.activator);
        return;
    }
    button.state = ButtonState::Out;
    button.isTouchable = button.isTouchOnly;
    if (button.isToggle) {
        useTargets(world, hulls, entity, button.activator);
    }
}

void returnButton(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity entity) {
    showAlternateTextures(world, entity, false);
    Button& button = *world.get<Button>(entity);
    button.state = ButtonState::GoingOut;
    moveTo(world, hulls, entity, button.out, button.speed);
}

} // namespace nx::game
