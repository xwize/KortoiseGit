// SPDX-FileCopyrightText: 2003-2006, 2008, 2011 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2012, 2014, 2016-2017, 2021-2023, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitFolderStatus.h, with the folder roll-up of src/Git/GitStatus.cpp
 * (GetMoreImportant, AdjustFolderStatus): the overlay state of every item in one folder, read with a single
 * git status. TortoiseGit caches whole working trees in TGitCache; Dolphin asks one folder at a time instead.
 */

#pragma once

#include "git/gitstatus.hpp"

#include <Dolphin/KVersionControlPlugin>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <optional>

namespace kgt
{

/**
 * The version state of the items directly inside one folder of a working tree, for Dolphin's overlays.
 *
 * Files map from their git status. A folder rolls up everything below it as TortoiseGit does: conflicted beats
 * modified (with unstaged changes beating fully staged ones, and added or deleted files counting as modified),
 * which beats normal; a folder git doesn't track shows as unversioned, or ignored if it holds only ignored files.
 */
class GitFolderStatus
{
public:
    using ItemVersion = KVersionControlPlugin::ItemVersion;

    /**
     * Builds the folder's state from git's answers, without running git.
     * @param prefix The folder's path relative to the working tree root, ending in '/' (empty for the root).
     * @param entries git status for the folder, with ignored files; paths are relative to the root.
     * @param trackedChildren Names of the folder's children in HEAD (git ls-tree), so a tracked folder with only
     *        untracked changes still shows as normal.
     */
    GitFolderStatus(const QString& prefix, const QList<GitStatusEntry>& entries, const QSet<QString>& trackedChildren);

    /**
     * Runs git in a folder (rev-parse, status and ls-tree, none taking locks) and builds its state.
     * @param directory The folder Dolphin shows.
     * @returns Its state.
     * @throws RuntimeError if the folder is not inside a working tree (including inside .git) or git fails.
     */
    [[nodiscard]] static GitFolderStatus fetch(const QString& directory);

    /**
     * @param name The item's file name, directly inside the folder.
     * @param isDir Whether the item is a folder, which rolls up what is below it.
     * @returns The state Dolphin draws for it.
     */
    [[nodiscard]] ItemVersion itemVersion(const QString& name, bool isDir) const;

    /**
     * @returns The repository's index file, whose changes (git add, commit) change the overlays. Empty unless
     *          built by fetch().
     */
    [[nodiscard]] const QString& indexFile() const;

    /**
     * @param entry One file's git status.
     * @returns The state Dolphin draws for that file.
     */
    [[nodiscard]] static ItemVersion fileVersion(const GitStatusEntry& entry);

private:
    /**
     * What is known about one child of the folder.
     */
    struct ChildState {
        std::optional<ItemVersion> own; // The child's own entry (a file, a submodule, or an ignored folder "name/")
        std::optional<ItemVersion> change; // The most important change below it, if it is a folder
        bool hasUntracked = false; // An untracked file is somewhere below it
        bool hasIgnored = false; // An ignored file or folder is somewhere below it
    };

    void addEntry(const QString& path, ItemVersion version);

    QString m_prefix;
    QHash<QString, ChildState> m_children;
    QSet<QString> m_trackedChildren;
    std::optional<ItemVersion> m_wholeFolder; // Set when the folder itself lies inside an ignored or untracked folder
    QString m_indexFile;
};

} // namespace kgt
