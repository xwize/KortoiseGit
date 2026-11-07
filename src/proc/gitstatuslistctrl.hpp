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
#include <QString>
#include <QStringList>
#include <QTreeView>

#include <memory>

class QCheckBox;
class QLabel;
class QSortFilterProxyModel;

namespace kgt
{

class GitCall;

/**
 * The changed-files list. The owning dialog sets the working tree and the show flags, places the Select all
 * checkbox and the counter label, calls reload() and acts on checkedEntries(). Double-clicking a file compares it
 * with HEAD in git difftool; the context menu offers Compare with base, Open, Open with... and Explore to; F5
 * reloads.
 */
class GitStatusListCtrl : public QTreeView
{
    Q_OBJECT

public:
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
     * Picks which entries are listed and which start checked (CGitStatusListCtrl::Show). Reloads if ignored files
     * are wanted and the last load left them out.
     * @param show The kinds of entries to list.
     * @param check The kinds of entries that start checked the first time they are seen.
     */
    void setShowFlags(GitStatusListModel::ShowFlags show, GitStatusListModel::ShowFlags check);

    /**
     * Runs git status and git diff --numstat in the background and refills the list when both are done
     * (CGitStatusListCtrl::GetStatus followed by Show). Any load still running is abandoned. F5 calls this.
     */
    void reload();

    /**
     * @returns Whether a load is running.
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

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void onStatusFinished();
    void onNumStatFinished();
    void finishLoad(const QList<NumStatEntry>& numStat);
    void failLoad(const Error& error);
    void updateCheckedCount();
    void onSelectButtonClicked();
    void onDoubleClicked(const QModelIndex& index);
    [[nodiscard]] QList<GitStatusListEntry> selectedEntries() const;
    void startDiff(const GitStatusListEntry& entry);
    void open(const GitStatusListEntry& entry);
    void openWith(const GitStatusListEntry& entry);
    void explore(const GitStatusListEntry& entry) const;
    [[nodiscard]] QStringList pathSpec() const;
    [[nodiscard]] bool isInPathFilter(const GitStatusEntry& entry) const;

    GitStatusListModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QPointer<QCheckBox> m_selectButton;
    QPointer<QLabel> m_statLabel;
    QString m_workingTree;
    QStringList m_pathFilter;
    std::unique_ptr<GitCall> m_statusCall;
    std::unique_ptr<GitCall> m_numStatCall;
    QList<GitStatusEntry> m_pendingStatus; // The status of the running load, waiting for its line counts
    QString m_errorMessage; // Shown in the empty list after a failed load
    QString m_emptyString; // Shown in the empty list after a load that found nothing
    bool m_isInitialRepo = false; // No commit yet (CGit::IsInitRepos): there is no HEAD to compare with
    bool m_hasIgnored = false; // Whether the last load listed ignored files
    bool m_includeStaged = false; // Whether staged changes outside m_pathFilter are listed too
};

} // namespace kgt
