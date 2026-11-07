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
#include <QMutex>
#include <QSet>
#include <QString>
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
     * @returns Its git state. Also remembers whether it can be added, for versionControlActions().
     */
    [[nodiscard]] ItemVersion itemVersion(const KFileItem& item) const override;

    /**
     * @param items The selected items, or the current directory when nothing is selected.
     * @returns The context menu actions for items inside a git working tree: Show log, and Add... if any item is
     *          unversioned or ignored, or is a folder holding unversioned files (TortoiseGit's MenuInfo for Add).
     *          Called on the main thread.
     */
    [[nodiscard]] QList<QAction *> versionControlActions(const KFileItemList& items) const override;

    /**
     * @param items The selected items outside any git working tree.
     * @returns No actions yet; TortoiseGit's Clone and Create repository here will go here.
     */
    [[nodiscard]] QList<QAction *> outOfVersionControlActions(const KFileItemList& items) const override;

private:
    /**
     * What itemVersion() found in one folder that Add can act on.
     */
    struct AddableItems {
        QSet<QString> names; // Children that are unversioned, ignored, or folders with unversioned files below
        bool hasUnversioned = false; // Whether an unversioned file lies somewhere in the folder
    };

    /**
     * @param items The items a context menu is for.
     * @returns Whether TortoiseGit would offer Add for them.
     */
    [[nodiscard]] bool canAdd(const KFileItemList& items) const;

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

    QAction *m_showLogAction;
    QAction *m_addAction;
    std::optional<GitFolderStatus> m_folderStatus; // Only touched on Dolphin's retrieval thread
    QString m_retrievalDirectory; // The folder of the running retrieval, without a trailing '/'; retrieval thread only
    mutable QMutex m_addableMutex; // Guards m_addable: written on the retrieval thread, read on the main thread
    mutable QHash<QString, AddableItems> m_addable; // By folder, without a trailing '/'
    KDirWatch m_indexWatch;
    QString m_watchedIndex; // The file m_indexWatch watches, if any
    QTimer m_refreshTimer; // Collects the several index writes of one git command into one refresh
};

} // namespace kgt
