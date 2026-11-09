// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/GitLogListBase.cpp (the list's data and texts)

#include "gitloglistmodel.hpp"

#include "gitstatuslistmodel.hpp"

#include <KLocalizedString>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QLocale>
#include <QStringList>

#include <cstddef>
#include <iterator>
#include <utility>

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

void GitLogListModel::appendFiltered(const QList<GitRevLoglist>& revs, const QList<bool>& visible)
{
    Q_ASSERT(revs.size() == visible.size());
    QList<GitRevLoglist> shown;
    std::vector<std::vector<Lanes::LaneType>> shownLanes;
    for (qsizetype i = 0; i < revs.size(); ++i) {
        std::vector<Lanes::LaneType> revLanes;
        updateLanes(revs.at(i), m_lns, revLanes, m_onlyFirstParent);
        if (visible.at(i)) {
            shown.append(revs.at(i));
            shownLanes.push_back(std::move(revLanes));
        }
    }
    if (shown.isEmpty()) {
        return;
    }
    const auto first = static_cast<int>(m_revs.size());
    beginInsertRows({}, first, first + static_cast<int>(shown.size()) - 1);
    m_revs.append(shown);
    m_lanes.insert(m_lanes.end(), std::make_move_iterator(shownLanes.begin()), std::make_move_iterator(shownLanes.end()));
    m_firstFreeLane = static_cast<int>(m_revs.size());
    endInsertRows();
}

void GitLogListModel::setOnlyFirstParent(bool onlyFirstParent)
{
    m_onlyFirstParent = onlyFirstParent;
}

void GitLogListModel::setGraphShown(bool shown)
{
    m_graphShown = shown;
}

bool GitLogListModel::isGraphShown() const
{
    return m_graphShown;
}

void GitLogListModel::setRefMask(unsigned int mask)
{
    m_refMask = mask;
}

bool GitLogListModel::isRefShown(const GitRef& ref) const
{
    switch (ref.type) {
    case GitRef::Type::LocalBranch:
        return m_refMask & ShowLocalBranches;
    case GitRef::Type::RemoteBranch:
        return m_refMask & ShowRemoteBranches;
    case GitRef::Type::Tag:
        return m_refMask & ShowTags;
    case GitRef::Type::Stash:
        return m_refMask & ShowStash;
    case GitRef::Type::Other:
        if (ref.name.startsWith(QLatin1String("refs/bisect/"))) {
            return m_refMask & ShowBisect;
        }
        return m_refMask & ShowOtherRefs;
    case GitRef::Type::Head:
        break;
    }
    return m_refMask & ShowOtherRefs;
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

void GitLogListModel::resetLanes()
{
    // The graph is drawn again from the top for the changed order
    m_lanes.assign(static_cast<std::size_t>(m_revs.size()), {});
    m_lns.clear();
    m_firstFreeLane = 0;
}

void GitLogListModel::setLane(int row) const
{
    // CLogDataVector::setLane: every row from the first not yet computed down to this one, in list order
    int i = m_firstFreeLane;
    for (; i <= row; ++i) {
        auto& rowLanes = m_lanes.at(static_cast<std::size_t>(i));
        if (rowLanes.empty()) {
            updateLanes(rev(i), m_lns, rowLanes, m_onlyFirstParent);
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

void GitLogListModel::setRebaseMode(bool rebaseMode)
{
    beginResetModel();
    m_rebaseMode = rebaseMode;
    endResetModel();
}

bool GitLogListModel::isRebaseMode() const
{
    return m_rebaseMode;
}

void GitLogListModel::setRefMap(const QHash<QString, QList<GitRef>>& refMap)
{
    for (int row = 0; row < m_revs.size(); ++row) {
        const QList<GitRef> refs = refMap.value(m_revs.at(row).hash);
        if (refs != m_revs.at(row).refs) {
            std::next(m_revs.begin(), row)->refs = refs;
            Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
        }
    }
}

void GitLogListModel::setRebaseAction(int row, unsigned int action)
{
    Q_ASSERT(row >= 0 && row < m_revs.size());
    std::next(m_revs.begin(), row)->rebaseAction = action;
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void GitLogListModel::removeRev(int row)
{
    Q_ASSERT(row >= 0 && row < m_revs.size());
    beginRemoveRows({}, row, row);
    m_revs.removeAt(row);
    resetLanes();
    endRemoveRows();
    if (m_rebaseMode && row > 0) {
        // The IDs above it count down from the row count
        Q_EMIT dataChanged(index(0, ActionsColumn), index(row - 1, ActionsColumn));
    }
}

void GitLogListModel::swapWithNext(int row)
{
    Q_ASSERT(row >= 0 && row + 1 < m_revs.size());
    // Moving the lower row up keeps the IDs, which belong to the positions, where they are
    beginMoveRows({}, row + 1, row + 1, {}, row);
    m_revs.swapItemsAt(row, row + 1);
    resetLanes();
    endMoveRows();
    if (m_rebaseMode) {
        Q_EMIT dataChanged(index(row, ActionsColumn), index(row + 1, ActionsColumn));
    }
}

QString GitLogListModel::rebaseActionName(unsigned int action)
{
    if (action & GitRevLoglist::RebaseEdit) {
        return i18n("Edit");
    }
    if (action & GitRevLoglist::RebaseSquash) {
        return i18n("Squash");
    }
    if (action & GitRevLoglist::RebasePick) {
        return i18n("Pick");
    }
    if (action & GitRevLoglist::RebaseSkip) {
        return i18n("Skip");
    }
    return i18n("Unknown");
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
        return displayData(index);
    case Qt::BackgroundRole:
    case Qt::ForegroundRole:
    case Qt::FontRole:
        return rebaseRowData(rowRev, role);
    case Qt::ToolTipRole:
        if (index.column() == ActionsColumn && !m_rebaseMode) {
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
        return m_rebaseMode ? i18n("REBASE") : i18n("Graph");
    case ActionsColumn:
        return m_rebaseMode ? i18n("ID") : i18n("Actions");
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

QVariant GitLogListModel::displayData(const QModelIndex& index) const
{
    const int row = index.row();
    const GitRevLoglist& rev = this->rev(row);
    switch (index.column()) {
    case GraphColumn:
        // LOGLIST_REBASE
        return m_rebaseMode ? QVariant(rebaseActionName(rev.rebaseAction & GitRevLoglist::RebaseModeMask)) : QVariant();
    case ActionsColumn:
        // LOGLIST_ID: numbered from the oldest, the last row (m_IsOldFirst is off)
        return m_rebaseMode ? QVariant(QString::number(m_revs.size() - row)) : QVariant();
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

QVariant GitLogListModel::rebaseRowData(const GitRevLoglist& rev, int role) const
{
    // CGitLogListBase::OnNMCustomdrawLoglist's colours for the rebase dialog
    if (!m_rebaseMode) {
        return {};
    }
    const unsigned int action = rev.rebaseAction;
    switch (role) {
    case Qt::BackgroundRole:
        if (action & GitRevLoglist::RebaseSquash) {
            return QBrush(QColor(156, 156, 156));
        }
        if (action & GitRevLoglist::RebaseEdit) {
            return QBrush(QColor(200, 200, 128));
        }
        return {};
    case Qt::ForegroundRole:
        if (action & (GitRevLoglist::RebaseDone | GitRevLoglist::RebaseSkip)) {
            return QBrush(QColor(128, 128, 128));
        }
        // Dark text on the light squash and edit colours, whatever the colour scheme
        if (action & (GitRevLoglist::RebaseSquash | GitRevLoglist::RebaseEdit)) {
            return QBrush(Qt::black);
        }
        return {};
    case Qt::FontRole:
        if (action & GitRevLoglist::RebaseCurrent) {
            QFont bold;
            bold.setBold(true);
            return bold;
        }
        return {};
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
