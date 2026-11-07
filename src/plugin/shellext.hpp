// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Plays the role of TortoiseGit src/TortoiseShell/ShellExt.h (CShellExt): the shell integration that adds the
 * context menu and launches kortoisegitproc for every command. Written fresh for Dolphin's plugin API.
 */

#pragma once

#include "gitfolderstatus.hpp"

#include <Dolphin/KVersionControlPlugin>
#include <KDirWatch>

#include <QHash>
#include <QTimer>
#include <QVariantList>

#include <optional>

class QAction;

namespace kgt
{

/**
 * The Dolphin version control plugin: TortoiseShell's counterpart. It adds TortoiseGit's context menu entries
 * and starts kortoisegitproc, detached, for each one. No dialog ever runs inside Dolphin's process.
 *
 * Overlays come from one git status per folder (GitFolderStatus), read on Dolphin's worker thread. The
 * repository's index file is watched, so staging or committing outside Dolphin refreshes them too.
 */
class ShellExt : public KVersionControlPlugin
{
    Q_OBJECT

public:
    /**
     * @param parent Owner of the plugin (Dolphin's version control observer).
     * @param args Plugin factory arguments; unused. The signature is the one KPluginFactory requires.
     */
    ShellExt(QObject *parent, const QVariantList& args);

    /**
     * @returns ".git", the entry whose presence marks a git working tree.
     */
    [[nodiscard]] QString fileName() const override;

    /**
     * Called by Dolphin on a worker thread before itemVersion() is asked for the items of @p directory.
     * Runs git for the folder and keeps the result for itemVersion(). Never throws: on any failure Dolphin is
     * told there is nothing to show.
     * @param directory The directory whose items will be queried, ending in '/'.
     * @returns Whether the folder's state could be read: false outside a working tree, inside .git, or when
     *          git fails, so Dolphin shows no overlays rather than wrong ones.
     */
    bool beginRetrieval(const QString& directory) override;

    /**
     * Called by Dolphin on a worker thread after the items of a directory have been queried.
     */
    void endRetrieval() override;

    /**
     * @param item The item Dolphin draws, directly inside the folder given to beginRetrieval().
     * @returns Its git state.
     */
    [[nodiscard]] ItemVersion itemVersion(const KFileItem& item) const override;

    /**
     * @param items The selected items, or the current directory when nothing is selected.
     * @returns The context menu actions for items inside a git working tree: Commit (for versioned items, showing
     *          the current branch), Show log, and Add... if an item is unversioned or ignored, or is a folder holding
     *          unversioned files.
     */
    [[nodiscard]] QList<QAction *> versionControlActions(const KFileItemList& items) const override;

    /**
     * @param items The selected items outside any git working tree.
     * @returns No actions yet; TortoiseGit's Clone and Create repository here will go here.
     */
    [[nodiscard]] QList<QAction *> outOfVersionControlActions(const KFileItemList& items) const override;

private:
    /**
     * Starts kortoisegitproc for the paths stored in @p action's data.
     * @param command TortoiseGit's command name, passed as --command.
     * @param action The triggered action; its data holds the selected paths.
     */
    void runCommand(const QString& command, const QAction *action);

    /**
     * Watches one repository's index (replacing the previous one), so git add or commit refreshes the overlays.
     * Runs on the main thread, where m_indexWatch lives.
     * @param indexFile The index file of the repository Dolphin shows.
     */
    void watchIndex(const QString& indexFile);

    /**
     * Keeps the state of a folder Dolphin shows for the context menu, next to those of the other folders it shows
     * (expanded folders in the details view). Runs on the main thread.
     * @param directory The folder, as given to beginRetrieval().
     * @param status Its state.
     */
    void setMenuStatus(const QString& directory, const GitFolderStatus& status);

    /**
     * @param directory A folder, without a trailing '/'.
     * @returns Its state as last read, or nullptr if it hasn't been read.
     */
    [[nodiscard]] const GitFolderStatus *menuStatus(const QString& directory) const;

    /**
     * Whether TortoiseGit's Commit entry applies (ITEMIS_INGIT or ITEMIS_FOLDERINGIT): some item is versioned.
     * Items of a folder whose state isn't known count as versioned.
     * @param items The selected items.
     * @returns True if the commit entry is shown.
     */
    [[nodiscard]] bool hasVersionedItem(const KFileItemList& items) const;

    /**
     * Whether TortoiseGit's Add entry applies (its MenuInfo: an unversioned item, a versioned folder, or an
     * ignored item), narrowed to folders that hold unversioned files. Items of a folder whose state isn't known
     * don't count.
     * @param items The selected items.
     * @returns True if the Add entry is shown.
     */
    [[nodiscard]] bool hasAddableItem(const KFileItemList& items) const;

    /**
     * Ported from CShellExt::InsertGitMenu: the Commit entry shows the current branch, "Git Commit -> "main"...".
     * @param path A selected path.
     * @returns The entry's text.
     */
    [[nodiscard]] static QString commitMenuText(const QString& path);

    QAction *m_commitAction;
    QAction *m_showLogAction;
    QAction *m_addAction;
    std::optional<GitFolderStatus> m_folderStatus; // Only touched on Dolphin's retrieval thread
    KDirWatch m_indexWatch;
    QString m_watchedIndex; // The file m_indexWatch watches, if any
    QTimer m_refreshTimer; // Collects the several index writes of one git command into one refresh
    QHash<QString, GitFolderStatus> m_menuStatus; // By folder without a trailing '/', for the context menu; main thread only
};

} // namespace kgt
