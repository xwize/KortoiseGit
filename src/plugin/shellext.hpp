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

#include <QMenu>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <memory>
#include <optional>

class QAction;

namespace kgt
{

class RemoteCacheLink;

/**
 * The Dolphin version control plugin: TortoiseShell's counterpart. It adds TortoiseGit's context menu entries
 * and starts kortoisegitproc, detached, for each one. No dialog ever runs inside Dolphin's process.
 *
 * Overlays come from one git status per folder (GitFolderStatus), read on Dolphin's worker thread. The
 * repository's index file is watched, so staging or committing outside Dolphin refreshes them too. When
 * kortoisegitcache knows the folder its answer is used instead, and it reports changes anywhere below the folder.
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
     * The arguments that hand a selection to kortoisegitproc. A selection too long for a command line goes in a
     * temporary file that kortoisegitproc deletes, as TortoiseGit always does (/pathfile, /deletepathfile).
     * @param paths The selected paths.
     * @returns The arguments, or nothing if the temporary file can't be written.
     */
    [[nodiscard]] static QStringList pathArguments(const QStringList& paths);

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
     * @param switches Switches after the paths, such as --abort.
     */
    void runCommand(const QString& command, const QAction *action, const QStringList& switches = {});

    /**
     * Refreshes the overlays if the cache reported a change to an item of the folder shown.
     * @param paths The items whose status changed.
     */
    void onCacheItemsChanged(const QStringList& paths);

    /**
     * Watches one repository's index (replacing the previous one), so git add or commit refreshes the overlays.
     * Runs on the main thread, where m_indexWatch lives.
     * @param indexFile The index file of the repository Dolphin shows.
     */
    void watchIndex(const QString& indexFile);

    /**
     * The entries TortoiseGit's menu gives the selection: Clone, Pull, Fetch, Push, Commit, Show log, Browse
     * References, Rebase, Abort Merge, Revert, Switch/Checkout, Merge, Create repository and Add, each where its MenuInfo
     * condition holds. Decided afresh on every request from the items' own paths, as CShellExt::Initialize and
     * QueryContextMenu do.
     * @param selected The selected paths, or the folder shown when nothing is selected.
     * @returns The top-level actions, in TortoiseGit's menu order, then the "KortoiseGit" submenu's action if the submenu
     *          has entries. Every entry's data is set to the paths.
     */
    [[nodiscard]] QList<QAction *> menuActions(const QStringList& selected) const;

    /**
     * Ported from CShellExt::InsertGitMenu: the Commit entry shows the current branch, "Git Commit -> "main"...",
     * or a shortened hash on a detached HEAD.
     * @param path A selected path.
     * @returns The entry's text.
     */
    [[nodiscard]] static QString commitMenuText(const QString& path);

    QAction *m_commitAction;
    QAction *m_createRepoAction;
    QAction *m_diffAction;
    QAction *m_prevDiffAction;
    QAction *m_showLogAction;
    QAction *m_revertAction;
    QAction *m_addAction;
    QAction *m_pullAction;
    QAction *m_fetchAction;
    QAction *m_pushAction;
    QAction *m_cloneAction;
    QAction *m_refBrowseAction;
    QAction *m_rebaseAction;
    QAction *m_switchAction;
    QAction *m_mergeAction;
    QAction *m_branchAction;
    QAction *m_tagAction;
    QAction *m_mergeAbortAction;
    std::unique_ptr<QMenu> m_gitMenu; // A QMenu's parent must be a widget, so it can't be a child of the plugin
    QAction *m_gitMenuAction;
    std::optional<GitFolderStatus> m_folderStatus; // Only touched on Dolphin's retrieval thread
    KDirWatch m_indexWatch;
    QString m_watchedIndex; // The file m_indexWatch watches, if any
    KDirWatch m_newRepoWatch; // Watches for the .git of a repository being created
    QString m_watchedNewRepo; // The .git m_newRepoWatch waits for, if any
    QTimer m_refreshTimer; // Collects the several index writes of one git command into one refresh
    RemoteCacheLink *m_cacheLink = nullptr; // Null when the status cache is compiled out; a child of the plugin
    QTimer m_cacheRefreshTimer; // Collects the cache's notifications for the folder shown into one refresh
    QString m_shownFolder; // The folder of the last retrieval, canonical; touched on the main thread only
    std::atomic_bool m_indexChanged = false; // The index changed since the last retrieval
};

} // namespace kgt
