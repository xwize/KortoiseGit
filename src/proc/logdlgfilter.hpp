// SPDX-FileCopyrightText: 2018-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/LogDlgFilter.h (CLogDlgFilter): the log dialog's search box.
 */

#pragma once

#include "filterhelper.hpp"

#include <QHash>
#include <QString>
#include <QStringList>

namespace kgt
{

struct GitRevLoglist;

/**
 * Matches a commit against the search text, looking at the fields selected in the filter menu.
 */
class LogDlgFilter : public FilterHelper
{
public:
    using FilterHelper::FilterHelper;

    /**
     * @returns Whether matching needs each commit's changed paths.
     */
    [[nodiscard]] bool needsPaths() const;

    /**
     * @returns Whether matching needs the annotated tags' text.
     */
    [[nodiscard]] bool needsTagInfo() const;

    /**
     * Ported from CLogDlgFilter::operator().
     * @param rev The commit.
     * @param paths Its changed paths (GitRevLoglist::m_SimpleFileList); only read if needsPaths().
     * @param tagInfo The annotated tags' text by full ref name; only read if needsTagInfo().
     * @returns Whether the commit is shown.
     */
    [[nodiscard]] bool operator()(const GitRevLoglist& rev, const QStringList& paths, const QHash<QString, QString>& tagInfo) const;
};

} // namespace kgt
