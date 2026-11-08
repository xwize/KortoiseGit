// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "utils/version.hpp"

#include "kortoisegit_buildversion.hpp"

namespace kgt
{

QString versionString()
{
    return QStringLiteral(KORTOISEGIT_BUILD_VERSION);
}

} // namespace kgt
