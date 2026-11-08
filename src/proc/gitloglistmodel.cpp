// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/GitLogListBase.cpp (the list's data and texts)

#include "gitloglistmodel.hpp"

#include "gitstatuslistmodel.hpp"

#include <KLocalizedString>

#include <QLocale>
#include <QStringList>

#include <cstddef>
#include <iterator>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

} // namespace

namespace kgt
{

GitLogListModel::GitLogListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void GitLogListModel::clear()
{
    beginResetModel();
    m_revs.clear();
    m_lanes.clear();
    m_lns.clear();
    m_firstFreeLane = 0;
    endResetModel();
}

void GitLogListModel::appendRevs(const QList<GitRevLoglist>& revs)
{
    if (revs.isEmpty()) {
        return;
    }
    const auto first = static_cast<int>(m_revs.size());
    beginInsertRows({}, first, first + static_cast<int>(revs.size()) - 1);
    m_revs.append(revs);
    m_lanes.resize(static_cast<std::size_t>(m_revs.size()));
    endInsertRows();
}

const GitRevLoglist& GitLogListModel::rev(int row) const
{
    Q_ASSERT(row >= 0 && row < m_revs.size());
    return m_revs.at(row);
}

const std::vector<Lanes::LaneType>& GitLogListModel::lanes(int row) const
{
    Q_ASSERT(row >= 0 && row < m_revs.size());
    const auto& rowLanes = m_lanes.at(static_cast<std::size_t>(row));
    if (rowLanes.empty()) {
        setLane(row);
    }
    return rowLanes;
}

void GitLogListModel::setLane(int row) const
{
    // CLogDataVector::setLane: every row from the first not yet computed down to this one, in list order
    int i = m_firstFreeLane;
    for (; i <= row; ++i) {
        auto& rowLanes = m_lanes.at(static_cast<std::size_t>(i));
        if (rowLanes.empty()) {
            // The first-parent-only view isn't ported yet (LOG_INFO_FIRST_PARENT)
            updateLanes(rev(i), m_lns, rowLanes, false);
        }
    }
    m_firstFreeLane = i;
}

void GitLogListModel::updateLanes(const GitRevLoglist& rev, Lanes& lns, std::vector<Lanes::LaneType>& lanes, bool onlyFirstParent)
{
    // CLogDataVector::updateLanes
    if (lns.isEmpty()) {
        lns.init(rev.hash);
    }

    bool isDiscontinuity = false;
    const bool isFork = lns.isFork(rev.hash, isDiscontinuity);
    const bool isMerge = rev.parents.size() > 1;
    const bool isInitial = rev.parents.isEmpty();

    if (isDiscontinuity) {
        lns.changeActiveLane(rev.hash); // Uses previous isBoundary state
    }

    lns.setBoundary(rev.isBoundary, isInitial); // Update must be here

    if (isFork) {
        lns.setFork(rev.hash);
    }
    if (isMerge) {
        lns.setMerge(rev.parents, onlyFirstParent);
    }
    if (isInitial) {
        lns.setInitial();
    }

    lanes = lns.lanes(); // Here lanes are snapshotted

    lns.nextParent(isInitial ? QString() : rev.parents.constFirst());

    if (isMerge) {
        lns.afterMerge();
    }
    if (isFork) {
        lns.afterFork();
        if (isInitial) {
            lns.setInitial();
        }
    }
    if (lns.isBranch()) {
        lns.afterBranch();
    }
}

void GitLogListModel::setFiles(int row, const QList<GitRevFiles>& files)
{
    Q_ASSERT(row >= 0 && row < m_revs.size());
    GitRevLoglist& rev = *std::next(m_revs.begin(), row);
    rev.files = files;
    rev.filesState = GitRevLoglist::FilesState::Loaded;
    // GitRevLoglist::m_Action: everything done to any file, against any parent
    rev.actions = {};
    for (const GitRevFiles& diff : files) {
        for (const GitStatusEntry& entry : diff.status) {
            rev.actions |= entry.actions;
        }
    }
    const QModelIndex cell = index(row, ActionsColumn);
    Q_EMIT dataChanged(cell, cell);
}

void GitLogListModel::setFilesFailed(int row)
{
    Q_ASSERT(row >= 0 && row < m_revs.size());
    std::next(m_revs.begin(), row)->filesState = GitRevLoglist::FilesState::Failed;
    const QModelIndex cell = index(row, ActionsColumn);
    Q_EMIT dataChanged(cell, cell); // The Actions column shows the error icon
}

QList<TGitPath::Action> GitLogListModel::shownActions()
{
    return {Action::Modified, Action::Added, Action::Deleted, Action::Replaced, Action::Unmerged};
}

int GitLogListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_revs.size());
}

int GitLogListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant GitLogListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const GitRevLoglist& rowRev = rev(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return displayData(rowRev, index.column());
    case Qt::ToolTipRole:
        if (index.column() == ActionsColumn) {
            return actionsToolTip(rowRev.actions);
        }
        return {};
    default:
        return {};
    }
}

Qt::ItemFlags GitLogListModel::flags(const QModelIndex& index) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
}

QVariant GitLogListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case GraphColumn:
        return i18n("Graph");
    case ActionsColumn:
        return i18n("Actions");
    case MessageColumn:
        return i18n("Message");
    case AuthorColumn:
        return i18n("Author");
    case DateColumn:
        return i18n("Date");
    default:
        return {};
    }
}

QVariant GitLogListModel::displayData(const GitRevLoglist& rev, int column)
{
    switch (column) {
    case MessageColumn:
        return rev.subject;
    case AuthorColumn:
        return rev.authorName;
    case DateColumn:
        // CLoglistUtils::FormatDateAndTime with the system's short date and time format
        return QLocale().toString(rev.authorDate, QLocale::ShortFormat);
    default:
        return {};
    }
}

QString GitLogListModel::actionsToolTip(TGitPath::Actions actions)
{
    // CGitLogListBase::GetToolTipText's LOGLIST_ACTION: the name of each action shown, in the icons' order
    QStringList names;
    for (const Action action : shownActions()) {
        if (actions & action) {
            names.append(GitStatusListModel::actionName(action));
        }
    }
    return names.join(QLatin1Char('\n'));
}

} // namespace kgt
