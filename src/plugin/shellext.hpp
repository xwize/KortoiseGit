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
     * @returns The context menu entries TortoiseGit gives them (see menuActions()).
     */
    [[nodiscard]] QList<QAction *> versionControlActions(const KFileItemList& items) const override;

    /**
     * Dolphin asks every enabled plugin for these when the folder it shows belongs to no version control system,
     * and lists all their answers. With nothing selected, @p items is the folder shown.
     * @param items The selected items. They can still be in a working tree or a bare repository of their own,
     *        such as a repository's folder seen from its parent.
     * @returns The context menu entries TortoiseGit gives them (see menuActions()): "Git Clone..." and "Git Create
     *          repository here..." for a folder in no repository.
     */
    [[nodiscard]] QList<QAction *> outOfVersionControlActions(const KFileItemList& items) const override;

    /**
     * Watches for a repository being created in a folder, and then tells Dolphin to look for it again so the
     * folder's overlays appear. Called when "Git Create repository here..." is chosen; one folder at a time.
     * @param folder The folder the repository will be created in.
     */
    void watchForNewRepository(const QString& folder);

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
     * The entries TortoiseGit's menu gives the selection: Clone, Pull, Fetch, Push, Commit, Show log, Revert, Create repository
     * and Add, each where its MenuInfo condition holds. Decided afresh on every request from the items' own paths,
     * as CShellExt::Initialize and QueryContextMenu do.
     * @param selected The selected paths, or the folder shown when nothing is selected.
     * @returns The actions, in TortoiseGit's menu order, their data set to the paths.
     */
    [[nodiscard]] QList<QAction *> menuActions(const QStringList& selected) const;

    /**
     * Ported from CShellExt::InsertGitMenu: the Commit entry shows the current branch, "Git Commit -> "main"...".
     * @param path A selected path.
     * @returns The entry's text.
     */
    [[nodiscard]] static QString commitMenuText(const QString& path);

    QAction *m_commitAction;
    QAction *m_createRepoAction;
    QAction *m_showLogAction;
    QAction *m_revertAction;
    QAction *m_addAction;
    QAction *m_pullAction;
    QAction *m_fetchAction;
    QAction *m_pushAction;
    QAction *m_cloneAction;
    QAction *m_refBrowseAction;
    std::optional<GitFolderStatus> m_folderStatus; // Only touched on Dolphin's retrieval thread
    KDirWatch m_indexWatch;
    QString m_watchedIndex; // The file m_indexWatch watches, if any
    KDirWatch m_newRepoWatch; // Watches for the .git of a repository being created
    QString m_watchedNewRepo; // The .git m_newRepoWatch waits for, if any
    QTimer m_refreshTimer; // Collects the several index writes of one git command into one refresh
};

} // namespace kgt
