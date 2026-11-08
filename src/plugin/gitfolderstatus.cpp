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
 * Like TortoiseGit's GitStatus::GetStatusRanking: when one path has two entries (git rm --cached lists the file
 * as both deleted and untracked), the higher ranked one is shown.
 * @param version A file's state.
 * @returns Its importance; higher wins.
 */
int fileRank(ItemVersion version)
{
    switch (version) {
    case KVersionControlPlugin::ConflictingVersion:
        return 6;
    case KVersionControlPlugin::LocallyModifiedUnstagedVersion:
    case KVersionControlPlugin::MissingVersion:
        return 5;
    case KVersionControlPlugin::LocallyModifiedVersion:
    case KVersionControlPlugin::AddedVersion:
    case KVersionControlPlugin::RemovedVersion:
        return 4;
    case KVersionControlPlugin::NormalVersion:
    case KVersionControlPlugin::UpdateRequiredVersion:
        return 3;
    case KVersionControlPlugin::UnversionedVersion:
        return 2;
    case KVersionControlPlugin::IgnoredVersion:
        return 1;
    }
    return 0;
}

/**
 * Like TortoiseGit's GitStatus::AdjustFolderStatus: what a changed file makes of the folders above it. Added and
 * deleted files make a folder modified; unstaged changes are kept apart from fully staged ones, as Dolphin can
 * show both.
 * @param version A file's state.
 * @returns The state it gives its folders, or nothing if it doesn't change them (normal, untracked, ignored).
 */
std::optional<ItemVersion> folderChange(ItemVersion version)
{
    switch (version) {
    case KVersionControlPlugin::ConflictingVersion:
        return KVersionControlPlugin::ConflictingVersion;
    case KVersionControlPlugin::LocallyModifiedUnstagedVersion:
    case KVersionControlPlugin::MissingVersion:
        return KVersionControlPlugin::LocallyModifiedUnstagedVersion;
    case KVersionControlPlugin::LocallyModifiedVersion:
    case KVersionControlPlugin::AddedVersion:
    case KVersionControlPlugin::RemovedVersion:
        return KVersionControlPlugin::LocallyModifiedVersion;
    default:
        return std::nullopt;
    }
}

/**
 * @param current The state so far, if any.
 * @param candidate Another state for the same item.
 * @returns Whichever ranks higher.
 */
ItemVersion moreImportant(std::optional<ItemVersion> current, ItemVersion candidate)
{
    return current && fileRank(*current) >= fileRank(candidate) ? *current : candidate;
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
        addEntry(entry.path, fileVersion(entry));
        if (!entry.origPath.isEmpty()) {
            // A rename also takes a file away from the source's folder
            addEntry(entry.origPath, KVersionControlPlugin::RemovedVersion);
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

KVersionControlPlugin::ItemVersion GitFolderStatus::itemVersion(const QString& name, bool isDir) const
{
    if (name == QLatin1String(".git")) {
        return KVersionControlPlugin::UnversionedVersion;
    }
    if (m_wholeFolder) {
        return *m_wholeFolder;
    }
    const ChildState child = m_children.value(name);
    if (child.own) {
        return *child.own;
    }
    if (!isDir) {
        // git status lists every untracked and ignored file, so an unlisted file is tracked and unchanged
        return KVersionControlPlugin::NormalVersion;
    }
    if (child.change) {
        return *child.change;
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
    switch (entry.kind) {
    case GitStatusKind::Unmerged:
        return KVersionControlPlugin::ConflictingVersion;
    case GitStatusKind::Untracked:
        return KVersionControlPlugin::UnversionedVersion;
    case GitStatusKind::Ignored:
        return KVersionControlPlugin::IgnoredVersion;
    case GitStatusKind::Changed:
    case GitStatusKind::RenamedOrCopied:
        break;
    }
    if (entry.worktree == FileState::Deleted) {
        return KVersionControlPlugin::MissingVersion;
    }
    if (entry.worktree != FileState::Unmodified) {
        return KVersionControlPlugin::LocallyModifiedUnstagedVersion;
    }
    switch (entry.index) {
    case FileState::Unmodified:
        return KVersionControlPlugin::NormalVersion;
    case FileState::Deleted:
        return KVersionControlPlugin::RemovedVersion;
    case FileState::Added:
    case FileState::Copied:
        return KVersionControlPlugin::AddedVersion;
    default:
        return KVersionControlPlugin::LocallyModifiedVersion;
    }
}

void GitFolderStatus::addEntry(const QString& path, ItemVersion version)
{
    // An ignored or untracked folder ("build/") that holds this whole folder
    if (path.endsWith(QLatin1Char('/')) && m_prefix.startsWith(path)) {
        m_wholeFolder = version;
        m_hasUnversioned = version == KVersionControlPlugin::UnversionedVersion;
        return;
    }
    if (!path.startsWith(m_prefix) || path.size() == m_prefix.size()) {
        return;
    }
    m_hasUnversioned = m_hasUnversioned || version == KVersionControlPlugin::UnversionedVersion;
    const QString relative = path.mid(m_prefix.size());
    const qsizetype slash = relative.indexOf(QLatin1Char('/'));
    // A file, or a folder git reports as a whole ("build/" when ignored, "nested/" for an untracked repository)
    if (slash < 0 || slash == relative.size() - 1) {
        ChildState& child = m_children.try_emplace(slash < 0 ? relative : relative.left(slash)).first->second;
        child.own = moreImportant(child.own, version);
        return;
    }
    ChildState& child = m_children.try_emplace(relative.left(slash)).first->second;
    if (version == KVersionControlPlugin::UnversionedVersion) {
        child.hasUntracked = true;
    } else if (version == KVersionControlPlugin::IgnoredVersion) {
        child.hasIgnored = true;
    } else if (const std::optional<ItemVersion> change = folderChange(version)) {
        child.change = moreImportant(child.change, *change);
    }
}

} // namespace kgt
