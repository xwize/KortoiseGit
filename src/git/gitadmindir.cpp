// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitAdminDir.cpp (GetWorktreeAdminDirPath, ReadGitLink, IsBareRepo) and src/Git/Git.cpp
 * (CGit::GetCurrentBranchFromFile).
 */

#include "gitadmindir.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace
{

constexpr QByteArrayView gitLinkPrefix = "gitdir: ";
constexpr QByteArrayView branchRefPrefix = "ref: refs/heads/";
constexpr qint64 maxLineLength = 4096; // TortoiseGit reads at most MAX_PATH characters

/**
 * @param file An open file.
 * @returns Its first line, without the line end or trailing whitespace.
 */
QByteArray readFirstLine(QFile& file)
{
    return file.readLine(maxLineLength).trimmed();
}

/**
 * Ported from GitAdminDir::ReadGitLink: follows a .git file ("gitdir: <path>").
 * @param dotGitFile The .git file.
 * @returns The directory it points to, or an empty string if it isn't a git link.
 */
QString readGitLink(const QString& dotGitFile)
{
    QFile file(dotGitFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray line = readFirstLine(file);
    if (!line.startsWith(gitLinkPrefix)) {
        return {};
    }
    const QString target = QFile::decodeName(line.mid(gitLinkPrefix.size()));
    // A relative link is relative to the directory holding the .git file
    return QDir::cleanPath(QFileInfo(dotGitFile).dir().absoluteFilePath(target));
}

} // namespace

namespace kgt
{

QString GitAdminDir::worktreeAdminDirPath(const QString& path)
{
    const QFileInfo info(path);
    QDir dir(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    while (true) {
        const QFileInfo dotGit(dir.absoluteFilePath(QStringLiteral(".git")));
        if (dotGit.isDir()) {
            return dotGit.absoluteFilePath();
        }
        if (dotGit.isFile()) {
            return readGitLink(dotGit.absoluteFilePath());
        }
        if (!dir.cdUp()) {
            return {};
        }
    }
}

bool GitAdminDir::isBareRepo(const QString& path)
{
    const QDir dir(path);
    // A .git directory has the same files, but belongs to a working tree
    if (path.isEmpty() || dir.dirName() == QLatin1String(".git")) {
        return false;
    }
    return QFileInfo(dir.filePath(QStringLiteral("HEAD"))).isFile() && QFileInfo(dir.filePath(QStringLiteral("config"))).isFile()
        && QFileInfo(dir.filePath(QStringLiteral("objects"))).isDir() && QFileInfo(dir.filePath(QStringLiteral("refs/heads"))).isDir();
}

QString GitAdminDir::currentBranchFromFile(const QString& adminDir)
{
    if (adminDir.isEmpty()) {
        return {};
    }
    QFile head(QDir(adminDir).absoluteFilePath(QStringLiteral("HEAD")));
    if (!head.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray line = readFirstLine(head);
    if (!line.startsWith(branchRefPrefix)) {
        // A detached HEAD
        return QStringLiteral("HEAD");
    }
    return QString::fromUtf8(line.mid(branchRefPrefix.size()));
}

} // namespace kgt
