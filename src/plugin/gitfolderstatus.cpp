// SPDX-FileCopyrightText: 2003-2008, 2011, 2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitFolderStatus.cpp and src/Git/GitStatus.cpp (GetStatusRanking)

#include "gitfolderstatus.hpp"

#include "git/gitadmindir.hpp"
#include "utils/runtimeerror.hpp"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>

#include <utility>

namespace
{

using namespace kgt;
using ItemVersion = KVersionControlPlugin::ItemVersion;

constexpr int retrievalTimeoutMs = 60000; // Per git command, on Dolphin's worker thread
constexpr int menuTimeoutMs = 3000; // Per git command, on Dolphin's UI thread, which a stuck git must not hang

/**
 * @param status What an item is.
 * @returns The state Dolphin draws for it.
 */
ItemVersion toItemVersion(GitWcStatus status)
{
    switch (status) {
    case GitWcStatus::Conflicted:
        return KVersionControlPlugin::ConflictingVersion;
    case GitWcStatus::ModifiedUnstaged:
        return KVersionControlPlugin::LocallyModifiedUnstagedVersion;
    case GitWcStatus::Missing:
        return KVersionControlPlugin::MissingVersion;
    case GitWcStatus::Modified:
        return KVersionControlPlugin::LocallyModifiedVersion;
    case GitWcStatus::Added:
        return KVersionControlPlugin::AddedVersion;
    case GitWcStatus::Deleted:
        return KVersionControlPlugin::RemovedVersion;
    case GitWcStatus::Normal:
        return KVersionControlPlugin::NormalVersion;
    case GitWcStatus::Ignored:
        return KVersionControlPlugin::IgnoredVersion;
    case GitWcStatus::Unversioned:
    case GitWcStatus::None:
        break;
    }
    return KVersionControlPlugin::UnversionedVersion;
}

/**
 * @param git Runs in the folder.
 * @returns The folder's children in HEAD, or none if HEAD has no commit yet.
 * @throws RuntimeError if git fails for any other reason than a missing HEAD.
 */
QSet<QString> trackedChildren(const Git& git)
{
    QByteArray output;
    try {
        // In a subfolder, ls-tree lists that subfolder's tree
        output = git.run({QStringLiteral("ls-tree"), QStringLiteral("-z"), QStringLiteral("--name-only"), QStringLiteral("HEAD")}, RunMode::ReadOnly);
    } catch (const RuntimeError& e) {
        switch (e.error().category) {
        case ErrorCategory::Git:
            // A new repository: nothing is committed, so nothing is tracked in HEAD
            return {};
        default:
            throw;
        }
    }
    QSet<QString> names;
    for (const QByteArray& name : output.split('\0')) {
        if (!name.isEmpty()) {
            names.insert(QString::fromUtf8(name));
        }
    }
    return names;
}

} // namespace

namespace kgt
{

GitFolderStatus::GitFolderStatus(const QString& prefix, const QList<GitStatusEntry>& entries, const QSet<QString>& trackedChildren)
    : m_prefix(prefix)
    , m_trackedChildren(trackedChildren)
{
    Q_ASSERT(prefix.isEmpty() || prefix.endsWith(QLatin1Char('/')));
    for (const GitStatusEntry& entry : entries) {
        addEntry(entry.path, GitStatus::wcStatus(entry));
        if (!entry.origPath.isEmpty()) {
            // A rename also takes a file away from the source's folder
            addEntry(entry.origPath, GitWcStatus::Deleted);
        }
    }
}

GitFolderStatus GitFolderStatus::fetch(const QString& directory)
{
    Git git(directory);
    git.setRunTimeout(retrievalTimeoutMs);
    return fetch(git, QStringLiteral("."));
}

GitFolderStatus GitFolderStatus::fetchItem(const QString& path)
{
    const QFileInfo info(path);
    Git git(info.absolutePath());
    git.setRunTimeout(menuTimeoutMs);
    return fetch(git, QStringLiteral(":(literal)") + info.fileName());
}

GitFolderStatus GitFolderStatus::fetch(const Git& git, const QString& pathspec)
{
    // --show-toplevel fails inside .git, which therefore gets no overlays. Asked for alone, as git writes it unquoted:
    // only the last newline ends it, the path may hold others
    QByteArray topLevel = git.run({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, RunMode::ReadOnly);
    if (topLevel.endsWith('\n')) {
        topLevel.chop(1);
    }
    const QString relative = QDir(QString::fromUtf8(topLevel)).relativeFilePath(QFileInfo(git.workingDirectory()).canonicalFilePath());
    const QString prefix = relative.isEmpty() || relative == QLatin1String(".") ? QString() : relative + QLatin1Char('/');

    // Only this folder (or item) and below: in a subfolder of a large repository, git doesn't scan the rest
    const QStringList statusArguments = GitStatus::arguments(IgnoredFiles::Include) + QStringList{QStringLiteral("--"), pathspec};
    const QList<GitStatusEntry> entries = getOrThrow(GitStatus::parse(git.run(statusArguments, RunMode::ReadOnly)));

    GitFolderStatus status(prefix, entries, trackedChildren(git));
    status.m_indexFile = QDir(GitAdminDir::worktreeAdminDirPath(git.workingDirectory())).filePath(QStringLiteral("index"));
    return status;
}

GitFolderStatus GitFolderStatus::fromCache(const CacheFolderStatus& cached, const QString& indexFile)
{
    Q_ASSERT(cached.names.size() == cached.states.size());
    GitFolderStatus status({}, {}, {});
    status.m_wholeFolder = cached.wholeFolder;
    status.m_hasUnversioned = cached.hasUnversioned;
    status.m_indexFile = indexFile;
    for (qsizetype i = 0; i < cached.names.size(); ++i) {
        const CacheItemState item = unpackItemState(cached.states.at(i));
        status.m_children.insert(cached.names.at(i),
                                 ChildState{.own = item.status, .change = GitWcStatus::None, .hasUntracked = item.hasUntracked, .hasIgnored = false});
    }
    return status;
}

KVersionControlPlugin::ItemVersion GitFolderStatus::itemVersion(const QString& name, bool isDir) const
{
    if (name == QLatin1String(".git")) {
        return KVersionControlPlugin::UnversionedVersion;
    }
    if (m_wholeFolder) {
        return toItemVersion(*m_wholeFolder);
    }
    const ChildState child = m_children.value(name);
    if (child.own) {
        return toItemVersion(*child.own);
    }
    if (!isDir) {
        // git status lists every untracked and ignored file, so an unlisted file is tracked and unchanged
        return KVersionControlPlugin::NormalVersion;
    }
    if (child.change != GitWcStatus::None) {
        return toItemVersion(child.change);
    }
    if (m_trackedChildren.contains(name)) {
        return KVersionControlPlugin::NormalVersion;
    }
    if (child.hasUntracked || !child.hasIgnored) {
        // Untracked content, or an empty folder git doesn't know about
        return KVersionControlPlugin::UnversionedVersion;
    }
    return KVersionControlPlugin::IgnoredVersion;
}

bool GitFolderStatus::hasUnversioned(const QString& name) const
{
    return m_children.value(name).hasUntracked;
}

bool GitFolderStatus::hasUnversioned() const
{
    return m_hasUnversioned;
}

const QString& GitFolderStatus::indexFile() const
{
    return m_indexFile;
}

KVersionControlPlugin::ItemVersion GitFolderStatus::fileVersion(const GitStatusEntry& entry)
{
    return toItemVersion(GitStatus::wcStatus(entry));
}

void GitFolderStatus::addEntry(const QString& path, GitWcStatus status)
{
    // An ignored or untracked folder ("build/") that holds this whole folder
    if (path.endsWith(QLatin1Char('/')) && m_prefix.startsWith(path)) {
        m_wholeFolder = status;
        m_hasUnversioned = status == GitWcStatus::Unversioned;
        return;
    }
    if (!path.startsWith(m_prefix) || path.size() == m_prefix.size()) {
        return;
    }
    m_hasUnversioned = m_hasUnversioned || status == GitWcStatus::Unversioned;
    const QString relative = path.mid(m_prefix.size());
    const qsizetype slash = relative.indexOf(QLatin1Char('/'));
    // A file, or a folder git reports as a whole ("build/" when ignored, "nested/" for an untracked repository)
    if (slash < 0 || slash == relative.size() - 1) {
        ChildState& child = m_children.try_emplace(slash < 0 ? relative : relative.left(slash)).first->second;
        child.own = child.own ? GitStatus::moreImportant(*child.own, status) : status;
        return;
    }
    ChildState& child = m_children.try_emplace(relative.left(slash)).first->second;
    if (status == GitWcStatus::Unversioned) {
        child.hasUntracked = true;
    } else if (status == GitWcStatus::Ignored) {
        child.hasIgnored = true;
    } else {
        child.change = GitStatus::moreImportant(child.change, GitStatus::adjustFolderStatus(status));
    }
}

} // namespace kgt
