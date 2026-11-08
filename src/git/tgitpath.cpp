// SPDX-FileCopyrightText: 2003-2008, 2025 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/TGitPath.cpp

#include "git/tgitpath.hpp"

#include <QDir>
#include <QFileInfo>

namespace kgt
{

TGitPath::Actions TGitPath::parseStatus(char status)
{
    switch (status) {
    case 'M':
    case 'T':
        return Action::Modified;
    case 'R':
        return Action::Replaced;
    case 'A':
        return Action::Added;
    case 'D':
    case 'K':
        return Action::Deleted;
    case 'U':
        return Action::Unmerged;
    case 'C':
        return Action::Copy;
    default:
        return {};
    }
}

QString TGitPath::resolvedPath(const QString& path)
{
    const QFileInfo info(path);
    const QString dir = QFileInfo(info.absolutePath()).canonicalFilePath();
    return dir.isEmpty() ? info.absoluteFilePath() : QDir(dir).filePath(info.fileName());
}

QStringList TGitPath::relativePaths(const QString& root, const QStringList& paths)
{
    QStringList relative;
    for (const QString& path : paths) {
        QString rel = QDir(root).relativeFilePath(resolvedPath(path));
        if (rel.isEmpty() || rel == QLatin1String(".")) {
            return {};
        }
        if (rel.endsWith(QLatin1Char('/'))) {
            rel.chop(1);
        }
        relative.append(rel);
    }
    return relative;
}

} // namespace kgt
