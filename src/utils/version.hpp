// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

namespace kgt
{

/**
 * The version this build reports. Plays the role of TortoiseGit src/version.h.
 * @return The release, such as 0.5.0, followed on any other build by its commit, such as 0.5.0+12.g1a2b3c4.dirty
 */
[[nodiscard]] QString versionString();

} // namespace kgt
