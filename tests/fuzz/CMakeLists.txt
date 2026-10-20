# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Fuzz targets of the parsers of external data (docs/18-testing-and-quality.md, "Sanitizers and
# analysis"). Each corpus directory holds synthetic seeds and every input that once crashed.
nxAddFuzzer(fuzzWad
    SOURCES
        wadFuzzer.cpp
    DEPENDS
        nx::assets
    CORPUS
        corpus/wad)
