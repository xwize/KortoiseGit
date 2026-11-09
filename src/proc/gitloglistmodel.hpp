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
        GraphColumn, // LOGLIST_GRAPH: the commit graph, drawn by GitLogList from lanes(); LOGLIST_REBASE in rebase mode
        ActionsColumn, // LOGLIST_ACTION: icons for what the commit did to its files, drawn by GitLogList; LOGLIST_ID in rebase mode
        MessageColumn, // LOGLIST_MESSAGE: the ref labels, drawn by GitLogList, then the subject
        AuthorColumn, // LOGLIST_AUTHOR
        DateColumn, // LOGLIST_DATE: the author date
        ColumnCount,
    };

    /**
     * Which kinds of ref get a label (CGitLogListBase::m_ShowRefMask), with TortoiseGit's LOGLIST_SHOW* values.
     */
    enum RefMask : unsigned int { // NOLINT(cppcoreguidelines-use-enum-class): Combined as bit flags, as in TortoiseGit
        ShowLocalBranches = 0x0001, // LOGLIST_SHOWLOCALBRANCHES
        ShowRemoteBranches = 0x0002, // LOGLIST_SHOWREMOTEBRANCHES
        ShowTags = 0x0004, // LOGLIST_SHOWTAGS
        ShowStash = 0x0008, // LOGLIST_SHOWSTASH
        ShowBisect = 0x0010, // LOGLIST_SHOWBISECT
        ShowOtherRefs = 0x0020, // LOGLIST_SHOWOTHERREFS
        ShowAllRefs = 0xFFFF, // LOGLIST_SHOWALLREFS
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
     * Adds the next commits of a walk of which only some are listed. Ported from CLogDataVector::append: every
     * commit goes through the graph's lanes, shown or not, so the compressed graph's lines stay joined.
     * @param revs The commits, in git's order.
     * @param visible Whether each one is listed; as long as revs.
     */
    void appendFiltered(const QList<GitRevLoglist>& revs, const QList<bool>& visible);

    /**
     * Draws only each commit's first parent in the graph (LOG_INFO_FIRST_PARENT). Takes effect for commits added
     * after it, so set it while the model is empty.
     * @param onlyFirstParent Whether to.
     */
    void setOnlyFirstParent(bool onlyFirstParent);

    /**
     * @param shown Whether the Graph column draws the graph (CGitLogListBase::OnNMCustomdrawLoglist's condition).
     */
    void setGraphShown(bool shown);

    /**
     * @returns Whether the Graph column draws the graph.
     */
    [[nodiscard]] bool isGraphShown() const;

    /**
     * @param mask RefMask bits saying which kinds of ref get a label.
     */
    void setRefMask(unsigned int mask);

    /**
     * Decides as CGitLogListBase::DrawTagBranchMessage's switch on the ref type does.
     * @param ref A ref.
     * @returns Whether it gets a label.
     */
    [[nodiscard]] bool isRefShown(const GitRef& ref) const;

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
     * Replaces every listed commit's refs (CGitLogListBase::ReloadHashMap's m_HashMap), redrawing those that changed.
     * @param refMap Each commit's refs by full hash; a commit left out has none.
     */
    void setRefMap(const GitRefMap& refMap);

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

    void setLane(int row) const;
    void resetLanes();
    static void updateLanes(const GitRevLoglist& rev, Lanes& lns, std::vector<Lanes::LaneType>& lanes, bool onlyFirstParent);

    QList<GitRevLoglist> m_revs;
    bool m_rebaseMode = false;
    bool m_onlyFirstParent = false; // LOG_INFO_FIRST_PARENT
    bool m_graphShown = true; // Whether the Graph column draws the graph
    unsigned int m_refMask = ShowAllRefs; // CGitLogListBase::m_ShowRefMask
    // The graph is computed lazily from const lanes(), as TortoiseGit does while painting
    mutable Lanes m_lns; // CLogDataVector::m_Lns: the lane state after row m_firstFreeLane - 1
    mutable std::vector<std::vector<Lanes::LaneType>> m_lanes; // GitRevLoglist::m_Lanes of each row; empty until computed
    mutable int m_firstFreeLane = 0; // CLogDataVector::m_FirstFreeLane: the first row not yet computed
};

} // namespace kgt
