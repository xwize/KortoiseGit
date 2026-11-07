// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * Ported from TortoiseGit src/Git/GitStatusListCtrl.cpp (CGitStatusListCtrl::Show, GetCellText, SelectAll),
 * src/Git/Git.cpp (CGit::GetWorkingTreeChanges) and src/Git/TGitPath.cpp (CTGitPath::GetActionName,
 * GetFileExtension).
 */

#include "gitstatuslistmodel.hpp"

#include <KLocalizedString>

#include <QIcon>
#include <QMimeDatabase>

#include <utility>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

/**
 * @param entry A status entry.
 * @returns Its actions, with Missing added for a file deleted from the working tree (CGit::GetWorkingTreeChanges
 *          marks those found by git diff --diff-filter=D).
 */
TGitPath::Actions entryActions(const GitStatusEntry& entry)
{
    TGitPath::Actions actions = entry.actions;
    if (entry.worktree == FileState::Deleted) {
        actions |= Action::Deleted | Action::Missing;
    }
    return actions;
}

/**
 * @param text A line count from git diff --numstat.
 * @returns It as a number for sorting: -1 for a binary file ("-"), -2 when unknown, so those sort first.
 */
int lineCountSortKey(const QString& text)
{
    if (text.isEmpty()) {
        return -2;
    }
    bool isNumber = false;
    const int count = text.toInt(&isNumber);
    return isNumber ? count : -1;
}

} // namespace

namespace kgt
{

GitStatusListModel::GitStatusListModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_show(Show::All)
{
}

QList<GitStatusListEntry> GitStatusListModel::makeEntries(const QList<GitStatusEntry>& status, const QList<NumStatEntry>& numStat)
{
    QHash<QString, const NumStatEntry *> numStatByPath;
    for (const NumStatEntry& stat : numStat) {
        numStatByPath.insert(stat.path, &stat);
    }

    QList<GitStatusListEntry> entries;
    entries.reserve(status.size());
    for (const GitStatusEntry& statusEntry : status) {
        GitStatusListEntry entry{
            .path = statusEntry.path,
            .origPath = statusEntry.origPath,
            .actions = entryActions(statusEntry),
            .linesAdded = {},
            .linesRemoved = {},
        };
        if (const NumStatEntry *stat = numStatByPath.value(statusEntry.path)) {
            entry.linesAdded = stat->added;
            entry.linesRemoved = stat->removed;
        }
        entries.append(std::move(entry));
    }
    return entries;
}

void GitStatusListModel::setEntries(const QList<GitStatusListEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    rebuildRows();
    endResetModel();
    Q_EMIT checkedCountChanged();
}

void GitStatusListModel::setShowFlags(ShowFlags show, ShowFlags check)
{
    beginResetModel();
    m_show = show;
    m_check = check;
    rebuildRows();
    endResetModel();
    Q_EMIT checkedCountChanged();
}

GitStatusListModel::ShowFlags GitStatusListModel::showFlags() const
{
    return m_show;
}

const QList<GitStatusListEntry>& GitStatusListModel::allEntries() const
{
    return m_entries;
}

const GitStatusListEntry& GitStatusListModel::entry(int row) const
{
    Q_ASSERT(row >= 0 && row < m_rows.size());
    return m_entries.at(m_rows.at(row));
}

QList<GitStatusListEntry> GitStatusListModel::checkedEntries() const
{
    QList<GitStatusListEntry> checked;
    for (const qsizetype index : m_rows) {
        if (isChecked(m_entries.at(index))) {
            checked.append(m_entries.at(index));
        }
    }
    return checked;
}

int GitStatusListModel::checkedCount() const
{
    int count = 0;
    for (const qsizetype index : m_rows) {
        if (isChecked(m_entries.at(index))) {
            ++count;
        }
    }
    return count;
}

void GitStatusListModel::setAllChecked(bool checked)
{
    for (const qsizetype index : std::as_const(m_rows)) {
        m_checkedByPath.insert(m_entries.at(index).path, checked);
    }
    if (!m_rows.isEmpty()) {
        Q_EMIT dataChanged(this->index(0, PathColumn), this->index(rowCount() - 1, PathColumn), {Qt::CheckStateRole});
    }
    Q_EMIT checkedCountChanged();
}

QString GitStatusListModel::actionName(TGitPath::Actions actions)
{
    // The order matters: a file can have several actions, and the first match names it
    if (actions & Action::Unmerged) {
        return i18n("Conflict");
    }
    if (actions & Action::Added) {
        return i18n("Added");
    }
    if (actions & Action::Missing) {
        return i18n("Missing");
    }
    if (actions & Action::Deleted) {
        return i18n("Deleted");
    }
    if (actions & Action::Merged) {
        return i18n("Merged");
    }
    if (actions & Action::Modified) {
        return i18n("Modified");
    }
    if (actions & Action::Replaced) {
        return i18n("Rename");
    }
    if (actions & Action::Copy) {
        return i18n("Copy");
    }
    if (actions & Action::AssumeValid) {
        return i18n("Assume valid/unchanged");
    }
    if (actions & Action::SkipWorktree) {
        return i18n("Skip worktree");
    }
    if (actions & Action::Ignore) {
        return i18n("Ignored");
    }
    // TortoiseGit has no name for an unversioned file either
    return i18n("Unknown");
}

QString GitStatusListModel::fileExtension(const QString& path)
{
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return dot > slash ? path.mid(dot) : QString();
}

int GitStatusListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int GitStatusListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

void GitStatusListModel::setHasCheckboxes(bool hasCheckboxes)
{
    beginResetModel();
    m_hasCheckboxes = hasCheckboxes;
    endResetModel();
}

bool GitStatusListModel::hasCheckboxes() const
{
    return m_hasCheckboxes;
}

QVariant GitStatusListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const GitStatusListEntry& rowEntry = entry(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return displayData(rowEntry, index.column());
    case Qt::CheckStateRole:
        if (m_hasCheckboxes && index.column() == PathColumn) {
            return isChecked(rowEntry) ? Qt::Checked : Qt::Unchecked;
        }
        return {};
    case Qt::DecorationRole:
        if (index.column() == PathColumn) {
            // TortoiseGit shows the system icon for the file type
            return QIcon::fromTheme(QMimeDatabase().mimeTypeForFile(rowEntry.path, QMimeDatabase::MatchExtension).iconName());
        }
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == LinesAddedColumn || index.column() == LinesRemovedColumn) {
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        }
        return {};
    case SortRole:
        return sortData(rowEntry, index.column());
    case PathRole:
        return rowEntry.path;
    case ActionsRole:
        return static_cast<unsigned int>(rowEntry.actions.toInt());
    default:
        return {};
    }
}

bool GitStatusListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!m_hasCheckboxes || !checkIndex(index, CheckIndexOption::IndexIsValid) || index.column() != PathColumn || role != Qt::CheckStateRole) {
        return false;
    }
    const bool checked = value.value<Qt::CheckState>() == Qt::Checked;
    m_checkedByPath.insert(entry(index.row()).path, checked);
    Q_EMIT dataChanged(index, index, {Qt::CheckStateRole});
    Q_EMIT checkedCountChanged();
    return true;
}

Qt::ItemFlags GitStatusListModel::flags(const QModelIndex& index) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
    if (m_hasCheckboxes && index.column() == PathColumn) {
        itemFlags |= Qt::ItemIsUserCheckable;
    }
    return itemFlags;
}

QVariant GitStatusListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case PathColumn:
        return i18n("Path");
    case ExtensionColumn:
        return i18n("Extension");
    case StatusColumn:
        return i18n("Status");
    case LinesAddedColumn:
        return i18n("Lines added");
    case LinesRemovedColumn:
        return i18n("Lines removed");
    default:
        return {};
    }
}

void GitStatusListModel::rebuildRows()
{
    m_rows.clear();
    const unsigned int show = m_show.toInt();
    const unsigned int check = m_check.toInt();
    for (qsizetype index = 0; index < m_entries.size(); ++index) {
        const GitStatusListEntry& listEntry = m_entries.at(index);
        const unsigned int actions = listEntry.actions.toInt();
        // A path seen for the first time gets its check state from the check flags (CGitStatusListCtrl::Show)
        if (!m_checkedByPath.contains(listEntry.path)) {
            m_checkedByPath.insert(listEntry.path, (actions & check) != 0);
        }
        if ((actions & show) != 0) {
            m_rows.append(index);
        }
    }
}

bool GitStatusListModel::isChecked(const GitStatusListEntry& entry) const
{
    return m_checkedByPath.value(entry.path);
}

QVariant GitStatusListModel::displayData(const GitStatusListEntry& entry, int column) const
{
    switch (column) {
    case PathColumn:
        if ((entry.actions & (Action::Replaced | Action::Copy)) && !entry.origPath.isEmpty()) {
            return i18nc("@item renamed file: new path (from old path)", "%1 (from %2)", entry.path, entry.origPath);
        }
        return entry.path;
    case ExtensionColumn:
        return fileExtension(entry.path);
    case StatusColumn:
        return actionName(entry.actions);
    case LinesAddedColumn:
        return entry.linesAdded;
    case LinesRemovedColumn:
        return entry.linesRemoved;
    default:
        return {};
    }
}

QVariant GitStatusListModel::sortData(const GitStatusListEntry& entry, int column)
{
    switch (column) {
    case LinesAddedColumn:
        return lineCountSortKey(entry.linesAdded);
    case LinesRemovedColumn:
        return lineCountSortKey(entry.linesRemoved);
    case StatusColumn:
        return actionName(entry.actions);
    case ExtensionColumn:
        return fileExtension(entry.path);
    default:
        return entry.path;
    }
}

} // namespace kgt
