// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitAdminDir.cpp (GetWorktreeAdminDirPath, GetAdminDirPath, HasAdminDir, IsAdminDirPath,
 * ReadGitLink, IsBareRepo) and src/Git/Git.cpp
 * (CGit::GetCurrentBranchFromFile).
 */

#include "gitadmindir.hpp"

#include "git/git.hpp"

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

QString GitAdminDir::commonAdminDirPath(const QString& worktreeAdminDir)
{
    QFile commonDir(QDir(worktreeAdminDir).filePath(QStringLiteral("commondir")));
    if (!commonDir.open(QIODevice::ReadOnly)) {
        return worktreeAdminDir;
    }
    const QString target = QFile::decodeName(readFirstLine(commonDir));
    return QDir::cleanPath(QDir(worktreeAdminDir).absoluteFilePath(target));
}

bool GitAdminDir::hasStashDir(const QString& worktreeAdminDir)
{
    const QDir adminDir(commonAdminDirPath(worktreeAdminDir));
    if (QFileInfo::exists(adminDir.filePath(QStringLiteral("refs/stash")))) {
        return true;
    }
    QFile packedRefs(adminDir.filePath(QStringLiteral("packed-refs")));
    if (!packedRefs.open(QIODevice::ReadOnly)) {
        return false;
    }
    // One "<hash> <ref>" per line, with "#" header and "^" peeled-tag lines in between. Read by line, unlike
    // TortoiseGit: this runs on Dolphin's thread and can stop at the entry
    while (!packedRefs.atEnd()) {
        const QByteArray line = packedRefs.readLine().trimmed();
        if (!line.startsWith('#') && !line.startsWith('^') && line.endsWith(" refs/stash")) {
            return true;
        }
    }
    return false;
}

QString GitAdminDir::projectTopDir(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    const QString dirName = QDir::cleanPath(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    // A .git dir or anything inside it is left out: only working tree files are of interest
    if (isAdminDirPath(dirName)) {
        return {};
    }
    QDir dir(dirName);
    while (true) {
        const QFileInfo dotGit(dir.filePath(QStringLiteral(".git")));
        if (dotGit.exists()) {
            const QString worktreeAdminDir = dotGit.isDir() ? dotGit.absoluteFilePath() : readGitLink(dotGit.absoluteFilePath());
            if (worktreeAdminDir.isEmpty()) {
                return {};
            }
            const QDir adminDir(commonAdminDirPath(worktreeAdminDir));
            // As in TortoiseGit, refs/heads must be a directory: the reftable format makes it a file
            if (!QFileInfo::exists(adminDir.filePath(QStringLiteral("HEAD"))) || !QFileInfo::exists(adminDir.filePath(QStringLiteral("config")))
                || !QFileInfo(adminDir.filePath(QStringLiteral("objects"))).isDir() || !QFileInfo(adminDir.filePath(QStringLiteral("refs"))).isDir()
                || !QFileInfo(adminDir.filePath(QStringLiteral("refs/heads"))).isDir()) {
                return {};
            }
            return dir.absolutePath();
        }
        // A bare repository inside a working tree is not part of it
        if (isBareRepo(dir.absolutePath()) || !dir.cdUp()) {
            return {};
        }
    }
}

bool GitAdminDir::isAdminDirPath(const QString& path)
{
    const QString cleaned = QDir::cleanPath(path);
    return cleaned.contains(QLatin1String("/.git/")) || cleaned.endsWith(QLatin1String("/.git")) || cleaned == QLatin1String(".git");
}

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
        // GetWorktreeAdminDirPath: a bare repository is its own admin directory
        if (isBareRepo(dir.absolutePath())) {
            return dir.absolutePath();
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

QString GitAdminDir::currentBranchFromFile(const QString& adminDir, bool fallback)
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
        return fallback && Git::isHash(line) ? QString::fromLatin1(line) : QStringLiteral("HEAD");
    }
    return QString::fromUtf8(line.mid(branchRefPrefix.size()));
}

} // namespace kgt
