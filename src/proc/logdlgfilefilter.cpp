// SPDX-FileCopyrightText: 2018-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/LogDlgFileFilter.cpp

#include "logdlgfilefilter.hpp"

namespace kgt
{

bool LogDlgFileFilter::operator()(const QString& path, const QString& oldPath) const
{
    if (!isFilterActive()) {
        return calculateFinalResult(true);
    }
    QString scratch = path;
    if (!oldPath.isEmpty()) {
        scratch += QLatin1Char('\n');
        scratch += oldPath;
    }
    return calculateFinalResult(match(scratch));
}

} // namespace kgt
