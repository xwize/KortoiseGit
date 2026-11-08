// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * The data half of TortoiseGit src/TortoiseProc/GitLogListBase.h (CGitLogListBase): the commits of the log, in
 * git's order, and the cell texts of its columns. GitLogList is the view over it.
 */

#pragma once

#include "git/gitrevloglist.hpp"

#include <QAbstractTableModel>
#include <QList>

namespace kgt
{

/**
 * The log's rows (CGitLogListBase::m_arShownList): one commit each, appended page by page while git log runs.
 */
class GitLogListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /**
     * The columns, in TortoiseGit's order (LOGLIST_*). The hidden-by-default ones (SHA-1, email, committer, commit
     * date, bug IDs) come with the rest of the log dialog.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): Qt's model API takes columns as int
    enum Column {
        GraphColumn, // LOGLIST_GRAPH: empty until the graph lanes are ported; LOGLIST_REBASE in rebase mode
        ActionsColumn, // LOGLIST_ACTION: icons for what the commit did to its files, drawn by GitLogList; LOGLIST_ID in rebase mode
        MessageColumn, // LOGLIST_MESSAGE: the ref labels, drawn by GitLogList, then the subject
        AuthorColumn, // LOGLIST_AUTHOR
        DateColumn, // LOGLIST_DATE: the author date
        ColumnCount,
    };

    /**
     * @param parent The owning object.
     */
    explicit GitLogListModel(QObject *parent = nullptr);

    /**
     * Removes every commit.
     */
    void clear();

    /**
     * Adds commits after the ones already listed.
     * @param revs The commits, in git's order.
     */
    void appendRevs(const QList<GitRevLoglist>& revs);

    /**
     * @param row A row of this model.
     * @returns The commit shown in that row.
     */
    [[nodiscard]] const GitRevLoglist& rev(int row) const;

    /**
     * Records a commit's files once they are fetched, and with them its actions for the Actions column.
     * @param row A row of this model.
     * @param files The changes against each parent, in parent order (one for a root commit).
     */
    void setFiles(int row, const QList<GitRevFiles>& files);

    /**
     * Records that a commit's files could not be fetched.
     * @param row A row of this model.
     */
    void setFilesFailed(int row);

    /**
     * Switches to the rebase dialog's columns (CGitLogListBase::m_IsRebaseReplaceGraph and m_IsIDReplaceAction):
     * the Graph column becomes "REBASE", naming each commit's rebase action, and the Actions column becomes "ID",
     * numbering the commits from the oldest. Rows are coloured by their rebase action.
     * @param rebaseMode Whether to show the rebase columns.
     */
    void setRebaseMode(bool rebaseMode);

    /**
     * @returns Whether the rebase columns are shown.
     */
    [[nodiscard]] bool isRebaseMode() const;

    /**
     * Sets a commit's rebase action (GitRevLoglist::GetRebaseAction() = action).
     * @param row A row of this model.
     * @param action GitRevLoglist::RebaseAction bits.
     */
    void setRebaseAction(int row, unsigned int action);

    /**
     * Removes one commit.
     * @param row A row of this model.
     */
    void removeRev(int row);

    /**
     * Swaps a commit with the one below it, as the rebase dialog's Up and Down buttons do (std::swap of two
     * entries of m_arShownList). Selections and persistent indexes follow the commits.
     * @param row A row of this model, not the last.
     */
    void swapWithNext(int row);

    /**
     * Ported from CGitLogListBase::GetRebaseActionName.
     * @param action GitRevLoglist::RebaseAction bits.
     * @returns The mode's name: "Edit", "Squash", "Pick", "Skip" or "Unknown".
     */
    [[nodiscard]] static QString rebaseActionName(unsigned int action);

    /**
     * @returns The actions the Actions column shows, in TortoiseGit's order (CGitLogListBase::DrawItem's
     *          LOGLIST_ACTION).
     */
    [[nodiscard]] static QList<TGitPath::Action> shownActions();

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    [[nodiscard]] QVariant displayData(const QModelIndex& index) const;
    [[nodiscard]] QVariant rebaseRowData(const GitRevLoglist& rev, int role) const;
    [[nodiscard]] static QString actionsToolTip(TGitPath::Actions actions);

    QList<GitRevLoglist> m_revs;
    bool m_rebaseMode = false;
};

} // namespace kgt
