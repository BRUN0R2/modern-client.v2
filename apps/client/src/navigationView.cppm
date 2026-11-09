// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What `nav_draw` draws over the scene of the map played (docs/offlineAgents.md).
export module app.client:navigationView;

import std;
import nx.core;
import nx.jobs;
import nx.ecs;
import nx.game;
import nx.agents;
import nx.console;
import nx.rhi;
import cs.client;
import :client;
import :session;

namespace app {

/// What `nav_draw` draws over the scene of the map played, while it draws: the agents' mesh, put on the GPU
/// once and kept by the scene, and the way each living agent follows on it, found again as the game ticks.
class NavigationView {
public:
    /// `nav_draw`: draws, or stops. Errors: InvalidArguments, CommandFailed (the map played, `session`, has
    /// no mesh; the cause told).
    [[nodiscard]] auto toggle(const nx::console::CommandLine& line, const Session* session)
        -> nx::Result<void, nx::console::ConsoleError>;

    /// Puts the mesh of `session` on the GPU with its `scene` in `list`, the first frame it is drawn, in
    /// cells as tall as `navigation` builds them; nothing while it does not draw, or without a mesh. Errors:
    /// GpuFailed, with the cause.
    [[nodiscard]] auto
    upload(Session& session, cs::Scene& scene, nx::rhi::CommandList& list,
           const nx::Result<nx::game::NavigationData, nx::console::ConsoleError>& navigation)
        -> nx::Result<void, ClientError>;

    /// What it draws over the frame of `session`, its ways found in jobs of `jobs`; nothing while it does not
    /// draw.
    [[nodiscard]] auto overlay(Session& session, nx::jobs::JobSystem& jobs) -> cs::SceneOverlay;

private:
    bool isDrawing = false;
    /// Kept for their room: the bodies of the agents, and those living that it shows.
    std::vector<std::pair<nx::ecs::Entity, const nx::agents::Agent*>> agentBodies;
    std::vector<cs::ShownAgent> shownAgents;
};

} // namespace app
