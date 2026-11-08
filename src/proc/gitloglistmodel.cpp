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
    endInsertRows();
}

const GitRevLoglist& GitLogListModel::rev(int row) const
{
    Q_ASSERT(row >= 0 && row < m_revs.size());
    return m_revs.at(row);
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
