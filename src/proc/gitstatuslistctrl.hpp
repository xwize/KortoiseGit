// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/GitStatusListCtrl.h (CGitStatusListCtrl): the list of changed files in the Add
 * and Commit dialogs, with a checkbox per file. TortoiseGit's CListCtrl becomes a QTreeView over a
 * GitStatusListModel, sorted through a proxy, fed asynchronously from git status and git diff --numstat.
 */

#pragma once

#include "gitstatuslistmodel.hpp"

#include "utils/error.hpp"

#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTreeView>

#include <QFlags>

#include <memory>

class QCheckBox;
class QLabel;
class QMenu;
class QSortFilterProxyModel;

namespace kgt
{

class GitCall;
class RevertProgressCommand;

/**
 * The changed-files list. The owning dialog sets the working tree and the show flags, places the Select all
 * checkbox and the counter label, calls reload() and acts on checkedEntries(). Double-clicking a file compares it
 * with HEAD in git difftool, or opens a conflicted one in git mergetool; the context menu offers Edit conflicts,
 * Resolved, Compare with base, Revert..., Open, Open with... and Explore to; F5 reloads.
 */
class GitStatusListCtrl : public QTreeView
{
    Q_OBJECT

public:
    /**
     * Context menu entries a dialog can turn off (TortoiseGit's GITSLC_POP*). Only those that are ported and that
     * some dialog of TortoiseGit's turns off are listed; the others are always offered.
     */
    enum class ContextMenu : unsigned int {
        Revert = 0x1, // GITSLC_POPREVERT
        Conflict = 0x2, // GITSLC_POPCONFLICT: Edit conflicts, which runs git mergetool
        Resolve = 0x4, // GITSLC_POPRESOLVE: Resolved
        All = 0xFFFFFFFF, // GITSLC_POPALL
    };
    Q_DECLARE_FLAGS(ContextMenus, ContextMenu)

    /**
     * @param parent The parent widget.
     */
    explicit GitStatusListCtrl(QWidget *parent = nullptr);
    ~GitStatusListCtrl() override;

    GitStatusListCtrl(const GitStatusListCtrl&) = delete;
    GitStatusListCtrl& operator=(const GitStatusListCtrl&) = delete;
    GitStatusListCtrl(GitStatusListCtrl&&) = delete;
    GitStatusListCtrl& operator=(GitStatusListCtrl&&) = delete;

    /**
     * @param root The top of the working tree to list. Takes effect on the next reload().
     */
    void setWorkingTree(const QString& root);

    /**
     * @returns The top of the working tree listed.
     */
    [[nodiscard]] const QString& workingTree() const;

    /**
     * Limits the list to some paths, as TortoiseGit does for the paths a dialog was opened for. Takes effect on
     * the next reload().
     * @param paths Pathspecs relative to the working tree root, or empty for the whole working tree.
     */
    void setPathFilter(const QStringList& paths);

    /**
     * Also lists staged changes outside the path filter (CGitStatusListCtrl::m_bIncludedStaged), as the commit
     * dialog needs: git commit takes everything staged, so all of it must be shown. Takes effect on the next
     * reload().
     * @param include Whether to list them.
     */
    void setIncludeStaged(bool include);

    /**
     * Lists the changes against HEAD~1 rather than HEAD, so the last commit's files are listed too, and compares and
     * reverts files against HEAD~1 (CGitStatusListCtrl::m_amend). The commit dialog sets it while amending. Takes
     * effect on the next reload().
     * @param amend Whether to compare with HEAD~1.
     */
    void setAmend(bool amend);

    /**
     * @returns Whether the last load found a branch without commits (CGit::IsInitRepos).
     */
    [[nodiscard]] bool isInitialRepo() const;

    /**
     * Picks which entries are listed and which start checked (CGitStatusListCtrl::Show). Reloads if ignored files
     * are wanted and the last load left them out.
     * @param show The kinds of entries to list.
     * @param check The kinds of entries that start checked the first time they are seen.
     */
    void setShowFlags(GitStatusListModel::ShowFlags show, GitStatusListModel::ShowFlags check);

    /**
     * Turns the checkboxes on or off (the bHasCheckboxes argument of CGitStatusListCtrl::Init).
     * @param hasCheckboxes Whether the files have a checkbox; true by default.
     */
    void setHasCheckboxes(bool hasCheckboxes);

    /**
     * Picks the context menu entries offered (the dwContextMenus argument of CGitStatusListCtrl::Init).
     * @param menus The entries; all of them by default.
     */
    void setContextMenus(ContextMenus menus);

    /**
     * Makes the list show the files of a commit rather than the working tree (CGitStatusListCtrl::m_CurrentVersion).
     * Comparing a file then compares the commit with the parent the change is against, the context menu offers
     * only that, and reload() does nothing: the owner fills the list with updateWithGitPathList(). A merge's files
     * are grouped by parent, under "Diff with parent N" (CGitStatusListCtrl::PrepareGroups).
     * @param revision The commit's hash, or empty for the working tree.
     * @param parents The commit's parents, first parent first; empty for a root commit.
     */
    void setCurrentVersion(const QString& revision, const QStringList& parents);

    /**
     * @returns The commit whose files are shown, or empty for the working tree.
     */
    [[nodiscard]] const QString& currentVersion() const;

    /**
     * Shows the given entries instead of loading them (CGitStatusListCtrl::UpdateWithGitPathList), and ends the
     * busy state.
     * @param entries All entries, listed or not.
     */
    void updateWithGitPathList(const QList<GitStatusListEntry>& entries);

    /**
     * Empties the list (CGitStatusListCtrl::Clear).
     */
    void clear();

    /**
     * Shows the busy string in the empty list while the owner loads its entries (CGitStatusListCtrl::SetBusy).
     * @param busy Whether the owner is loading.
     */
    void setBusy(bool busy);

    /**
     * Sets what the empty list says while busy (CGitStatusListCtrl::SetBusyString).
     * @param text Translated text; "Please wait..." by default.
     */
    void setBusyString(const QString& text);

    /**
     * Runs git status and git diff --numstat in the background and refills the list when both are done
     * (CGitStatusListCtrl::GetStatus followed by Show). Any load still running is abandoned. F5 calls this.
     */
    void reload();

    /**
     * @returns Whether a load, or a revert from the context menu, is running.
     */
    [[nodiscard]] bool isBusy() const;

    /**
     * Wires a tristate checkbox to check or uncheck every listed entry, and keeps its state in step with the list
     * (CGitStatusListCtrl::SetSelectButton).
     * @param button The dialog's "Select/deselect all" checkbox. It is not owned.
     */
    void setSelectButton(QCheckBox *button);

    /**
     * Keeps a label showing "n files selected, m files total" (CGitStatusListCtrl::SetStatLabel).
     * @param label The dialog's label. It is not owned.
     */
    void setStatLabel(QLabel *label);

    /**
     * Sets what the list says when it has nothing to show (CGitStatusListCtrl::SetEmptyString).
     * @param text Translated text; "File list is empty" by default.
     */
    void setEmptyString(const QString& text);

    /**
     * @returns The model holding every entry, in git's order.
     */
    [[nodiscard]] GitStatusListModel *statusModel() const;

    /**
     * @returns The listed entries that are checked, in git's order.
     */
    [[nodiscard]] QList<GitStatusListEntry> checkedEntries() const;

    /**
     * @returns Whether the list holds a file in conflict (CGitStatusListCtrl::GetConflictedCount).
     */
    [[nodiscard]] bool hasConflicts() const;

    /**
     * Selects the first file in conflict and shows it.
     */
    void selectFirstConflict();

    /**
     * @returns Whether a Revert or Resolved started from the context menu is changing the repository.
     */
    [[nodiscard]] bool isModifying() const;

    /**
     * Ported from the IDGITLC_RESOLVECONFLICT entry and ResolveProgressCommand (ResolveWith::Current): asks, then
     * marks the selected conflicted files as resolved with git add -f, and reloads. Files not in conflict are
     * skipped.
     */
    void resolveSelected();

    /**
     * @param entry An entry of this list.
     * @returns Its absolute path.
     */
    [[nodiscard]] QString absolutePath(const GitStatusListEntry& entry) const;

Q_SIGNALS:
    /**
     * A reload() has finished and the list shows its result.
     */
    void loaded();

    /**
     * A reload() failed; the list is left empty and shows the message.
     * @param error What went wrong.
     */
    void loadFailed(const kgt::Error& error);

    /**
     * The number of checked entries, or of listed entries, may have changed.
     */
    void checkedCountChanged();

    /**
     * A revert from the context menu has finished, and the list is reloading. If it failed, the error has been
     * shown.
     */
    void reverted();

    /**
     * Resolved has finished, and the list is reloading. If it failed, the error has been shown.
     */
    void resolved();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void onStatusFinished();
    void onIndexDiffFinished();
    void loadNumStat();
    void onNumStatFinished();
    void finishLoad(const QList<NumStatEntry>& numStat);
    void failLoad(const Error& error);
    void updateCheckedCount();
    void onSelectButtonClicked();
    void onDoubleClicked(const QModelIndex& index);
    [[nodiscard]] QList<GitStatusListEntry> selectedEntries() const;
    void startDiff(const GitStatusListEntry& entry);
    [[nodiscard]] QString parentRevision(int parentNo) const;
    void showGroups();
    void open(const GitStatusListEntry& entry);
    void openWith(const GitStatusListEntry& entry);
    void explore(const GitStatusListEntry& entry) const;
    void revertSelected();
    void onRevertFinished();
    void addConflictEntries(QMenu& menu, int selectedCount);
    void editConflict(const GitStatusListEntry& entry);
    void onResolveFinished();
    [[nodiscard]] const GitStatusListEntry *focusedEntry() const;
    [[nodiscard]] bool isPathFilterGivenToGit() const;
    [[nodiscard]] QStringList pathSpec() const;
    [[nodiscard]] QString baseRevision() const;
    [[nodiscard]] bool isInPathFilter(const GitStatusEntry& entry) const;

    GitStatusListModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QPointer<QCheckBox> m_selectButton;
    QPointer<QLabel> m_statLabel;
    QString m_workingTree;
    QStringList m_pathFilter;
    QSet<QString> m_pathFilterSet; // The same, to look paths up in
    std::unique_ptr<GitCall> m_statusCall;
    std::unique_ptr<GitCall> m_indexDiffCall; // The index against HEAD~1, while amending
    std::unique_ptr<GitCall> m_numStatCall;
    std::unique_ptr<RevertProgressCommand> m_revertCommand; // The running revert, if any
    std::unique_ptr<GitCall> m_resolveCall; // The running git add of Resolved, if any
    QList<GitStatusEntry> m_pendingStatus; // The status of the running load, waiting for its line counts
    QString m_errorMessage; // Shown in the empty list after a failed load
    QString m_emptyString; // Shown in the empty list after a load that found nothing
    QString m_busyString; // Shown in the empty list while busy
    QString m_currentVersion; // The commit whose files are listed; empty for the working tree
    QStringList m_parentVersions; // The parents of m_currentVersion; empty for a root commit
    bool m_isInitialRepo = false; // No commit yet (CGit::IsInitRepos): there is no HEAD to compare with
    bool m_hasIgnored = false; // Whether the last load listed ignored files
    bool m_includeStaged = false; // Whether staged changes outside m_pathFilter are listed too
    bool m_amend = false; // Changes are against HEAD~1 (m_amend)
    bool m_isBusy = false; // The owner is loading the entries (setBusy)
    bool m_hasDirectFiles = false; // The direct files have been taken from the path filter (m_setDirectFiles)
    ContextMenus m_contextMenus = ContextMenu::All; // TortoiseGit's m_dwContextMenus
};

} // namespace kgt

Q_DECLARE_OPERATORS_FOR_FLAGS(kgt::GitStatusListCtrl::ContextMenus)
