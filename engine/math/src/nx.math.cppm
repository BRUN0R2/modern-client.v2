// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.math — geometry of the Next Engine (docs/01-arquitetura.md, layer 1).
///
/// Vectors, matrices, quaternions, GoldSrc angles, axis-aligned boxes and planes. Every type is a
/// trivially copyable aggregate, usable as an ECS component and in GPU buffers.
///
/// Determinism (docs/07-simulacao-e-fisica.md, section 6): the arithmetic is plain IEEE float, and
/// the build forbids contracting `a * b + c` into a fused multiply-add and fast-math, so a result
/// depends only on the inputs, never on the thread or the worker count. Sine, cosine and arc
/// functions are evaluated in double precision and rounded once to float, which hides last-bit
/// differences between C runtimes.
export module nx.math;

export import :vector;
export import :matrix;
export import :angles;
export import :quaternion;
export import :bounds;
export import :plane;
