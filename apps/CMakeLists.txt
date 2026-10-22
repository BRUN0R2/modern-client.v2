# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# The programs players and servers run (docs/17-build-and-tools.md, section 1).
if(NX_BUILD_CLIENT)
    add_subdirectory(client)
endif()
