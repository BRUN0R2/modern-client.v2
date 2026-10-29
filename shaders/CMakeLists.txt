# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The engine's shaders, compiled to bin/shaders/ (nxAddShaders, cmake/nxTargets.cmake).
nxAddShaders(nxShaders
    SOURCES
        world.slang
        studio.slang
        sprite.slang
        particle.slang
        overlay.slang
        screen.slang
        post.slang
        probe.slang
        test/fill.slang
        test/sample.slang
        test/sampleLayer.slang)
