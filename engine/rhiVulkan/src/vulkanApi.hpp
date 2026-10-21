// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// The Vulkan API as the backend's implementation units see it, included by their global module
// fragments: volk's declarations, and the surface functions of each window system. A global module
// fragment may only include, so the declarations below live here.

#if defined(_WIN32)
#include <windows.h> // WIN32_LEAN_AND_MEAN and NOMINMAX come from engine/rhiVulkan/CMakeLists.txt
#endif
#include <volk.h>
#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#else
// The types that vulkan_xlib.h and vulkan_wayland.h name, as Xlib and Wayland declare them, without
// Xlib.h and wayland-client.h: their macros (None, Always, Bool, Status...) would leak into the
// units that include this.
struct _XDisplay; // NOLINT(bugprone-reserved-identifier): Xlib's own name
using Display = _XDisplay;
using XID = unsigned long;
using Window = XID;
using VisualID = unsigned long;
struct wl_display;
struct wl_surface;
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xlib.h>
#endif
