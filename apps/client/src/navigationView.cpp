// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.jobs;
import nx.ecs;
import nx.game;
import nx.agents;
import nx.console;
import nx.rhi;
import cs.server;
import cs.client;

namespace app {

auto NavigationView::toggle(const nx::console::CommandLine& line, const Session* session)
    -> nx::Result<void, nx::console::ConsoleError> {
    using nx::console::ConsoleError;
    if (!line.arguments().empty()) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: nav_draw");
    }
    if (!isDrawing && session != nullptr && !session->agentMesh) {
        return nx::fail(ConsoleError::CommandFailed,
                        std::format("nav_draw: {} has no navigation mesh: {}", session->map->name(),
                                    nx::describe(session->agentMesh.error())));
    }
    isDrawing = !isDrawing;
    return {};
}

auto NavigationView::upload(Session& session, cs::Scene& scene, nx::rhi::CommandList& list,
                            const nx::Result<nx::game::NavigationData, nx::console::ConsoleError>& navigation)
    -> nx::Result<void, ClientError> {
    if (!isDrawing || !session.agentMesh || !navigation || session.navigationDrawing) {
        return {};
    }
    // The mesh, put on the GPU once: the session's scene keeps it as long as the drawing lives.
    const cs::NavigationDrawing& drawing =
        session.navigationDrawing.emplace(*session.agentMesh, navigation->build.cellHeight);
    if (auto kept = scene.keepOverlay(list, drawing.mesh()); !kept) {
        return nx::fail(ClientError::GpuFailed, std::move(kept.error()));
    }
    return {};
}

auto NavigationView::keepOn(cs::Scene& scene, const Session& session, nx::rhi::CommandList& list)
    -> nx::Result<void, ClientError> {
    if (!session.navigationDrawing) {
        return {};
    }
    if (auto kept = scene.keepOverlay(list, session.navigationDrawing->mesh()); !kept) {
        return nx::fail(ClientError::GpuFailed, std::move(kept.error()));
    }
    return {};
}

auto NavigationView::overlay(Session& session, nx::jobs::JobSystem& jobs) -> cs::SceneOverlay {
    if (!isDrawing || !session.navigationDrawing) {
        return {};
    }
    cs::NavigationDrawing& drawing = *session.navigationDrawing;
    // The ways change only as the game ticks: found again once a tick, not once a frame.
    const nx::GameTime now = session.game->time().now;
    if (!drawing.showsWaysOf(now)) {
        agentBodies.clear();
        session.game->forEachEntityComponent<nx::agents::Agent>(
            [this](nx::ecs::Entity body, const nx::agents::Agent& agent) {
                agentBodies.emplace_back(body, &agent);
            });
        // A dead body decides nothing: its agent's last decision and corridor are of its last life.
        shownAgents.clear();
        for (const auto& [body, agent] : agentBodies) {
            if (const cs::BodyStatus status = session.game->bodyStatusOf(body); status.isAlive) {
                shownAgents.push_back(cs::ShownAgent{.agent = agent, .team = status.team});
            }
        }
        drawing.showWays(jobs, now, shownAgents);
        agentBodies.clear(); // borrowed only until the world's entities change
        shownAgents.clear();
    }
    return drawing.overlay();
}

} // namespace app
