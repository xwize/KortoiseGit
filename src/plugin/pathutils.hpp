// SPDX-FileCopyrightText: 2003-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Utils/PathUtils.h (GetAppDirectory): finds the programs installed beside the plugin.
 */

#pragma once

#include <QString>

namespace kgt
{

/**
 * Finds one of our executables. From the build tree the plugin is bin/dolphin/vcs/kortoisegitplugin.so and the programs
 * are in bin/, so that is tried first; then the install location, then $PATH.
 * @param name The executable's file name.
 * @returns The executable's path, or an empty string if none was found.
 */
[[nodiscard]] QString findSiblingExecutable(const QString& name);

} // namespace kgt
