// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * The data half of TortoiseGit src/TortoiseProc/GitLogListBase.h (CGitLogListBase): the commits of the log, in
 * git's order, and the cell texts of its columns. GitLogList is the view over it.
 */

#pragma once

#include "lanes.hpp"

#include "git/gitrevloglist.hpp"

#include <QAbstractTableModel>
#include <QList>

#include <vector>

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
        GraphColumn, // LOGLIST_GRAPH: the commit graph, drawn by GitLogList from lanes()
        ActionsColumn, // LOGLIST_ACTION: icons for what the commit did to its files, drawn by GitLogList
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
     * The commit graph's lanes on a row, computed on first use for it and every row above it, in list order
     * (CGitLogListBase::DrawGraph calling CLogDataVector::setLane). TortoiseGit keeps them on the revision
     * (GitRevLoglist::m_Lanes); here the model keeps them beside it, since the git layer doesn't know Lanes.
     * @param row A row of this model.
     * @returns The type of each lane, left to right.
     */
    [[nodiscard]] const std::vector<Lanes::LaneType>& lanes(int row) const;

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
    [[nodiscard]] static QVariant displayData(const GitRevLoglist& rev, int column);
    [[nodiscard]] static QString actionsToolTip(TGitPath::Actions actions);

    void setLane(int row) const;
    static void updateLanes(const GitRevLoglist& rev, Lanes& lns, std::vector<Lanes::LaneType>& lanes, bool onlyFirstParent);

    QList<GitRevLoglist> m_revs;
    // The graph is computed lazily from const lanes(), as TortoiseGit does while painting
    mutable Lanes m_lns; // CLogDataVector::m_Lns: the lane state after row m_firstFreeLane - 1
    mutable std::vector<std::vector<Lanes::LaneType>> m_lanes; // GitRevLoglist::m_Lanes of each row; empty until computed
    mutable int m_firstFreeLane = 0; // CLogDataVector::m_FirstFreeLane: the first row not yet computed
};

} // namespace kgt
