// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The implementation of the Vulkan Memory Allocator, a header-only library: built once, apart from
// the engine's code and its warnings. The device hands it every function through
// vmaImportVulkanFunctionsFromVolk, so it links to no Vulkan function of its own.

#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
