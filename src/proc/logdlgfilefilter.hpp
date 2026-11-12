// SPDX-FileCopyrightText: 2018-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/LogDlgFileFilter.h (CLogDlgFileFilter): the Changed Files dialog's
 * filter box.
 */

#pragma once

#include "filterhelper.hpp"

#include <QString>

namespace kgt
{

/**
 * Matches a changed file's path, and the path it was renamed or copied from, against the filter text.
 */
class LogDlgFileFilter : public FilterHelper
{
public:
    using FilterHelper::FilterHelper;

    /**
     * Ported from CLogDlgFileFilter::operator().
     * @param path The file's path.
     * @param oldPath The path it was renamed or copied from; empty if it wasn't.
     * @returns Whether the file is shown.
     */
    [[nodiscard]] bool operator()(const QString& path, const QString& oldPath) const;
};

} // namespace kgt
