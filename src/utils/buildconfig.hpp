// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace kgt
{

/**
 * True in a build with assertions enabled (Debug), false in a release build.
 * Debug-only code uses if constexpr (kgt::isDebugBuild) instead of #ifdef, so it is compiled in every
 * configuration. This is the only place that reads NDEBUG.
 */
#ifdef NDEBUG
inline constexpr bool isDebugBuild = false;
#else
inline constexpr bool isDebugBuild = true;
#endif

} // namespace kgt
