// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * Ported from TortoiseGit src/Git/GitStatusListCtrl.cpp (CGitStatusListCtrl::Show, GetCellText, SelectAll,
 * PrepareGroups, SetItemGroup, the colours of OnNMCustomdraw),
 * src/Git/Git.cpp (CGit::GetWorkingTreeChanges) and src/Git/TGitPath.cpp (CTGitPath::GetActionName,
 * GetFileExtension).
 */

#include "gitstatuslistmodel.hpp"

#include <KLocalizedString>

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QMimeDatabase>
#include <QPalette>

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

/**
 * The group of a working tree entry, in PrepareGroups' order: the values are SetItemGroup's group ids.
 */
enum class StatusGroup : qsizetype {
    ModifiedFiles,
    NotVersionedFiles,
    IgnoredFiles,
    IgnoreLocalChanges,
};

/**
 * The colour of a kind of entry.
 */
struct ActionColor {
    TGitPath::Actions actions; // Any of these
    QRgb color;
};

/**
 * CGitStatusListCtrl::OnNMCustomdraw's colours, in its order: the first match wins, and anything else keeps the
 * normal text colour. Its code decides, not the comment above it, which has replaced entries brown.
 *
 * TortoiseGit's hues (Colors.cpp) as pastels, one set for both themes in place of CTheme::GetThemeColor's lightness
 * inversion, as branchLineColors in gitloglist.cpp does. OKLCh lightness 0.60 is a little below that palette's
 * because these are file names: it is where the contrast is about equal on white and on Breeze Dark, near 4:1 on
 * both. TortoiseGit's Modified and Renamed are both blue, and its Deleted is a darker Conflict, so Renamed moves
 * towards cyan and Deleted towards brown to tell them apart.
 */
constexpr std::array<ActionColor, 6> actionColors{{
    {.actions = Action::Unmerged, .color = 0xffc25d58}, // Conflict, red
    {.actions = Action::Modified, .color = 0xff5581c1}, // Modified, blue
    {.actions = Action::Added | Action::Copy, .color = 0xffa763ab}, // Added, purple
    {.actions = Action::Deleted, .color = 0xffa27552}, // Deleted, brown
    {.actions = Action::Replaced, .color = 0xff318ea2}, // Renamed, cyan-blue
    {.actions = Action::Merged, .color = 0xff4d9351}, // Merged, green
}};

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

/**
 * @param actions What happened to a file.
 * @returns The colour of its text, or an invalid QVariant for the normal one (CGitStatusListCtrl::OnNMCustomdraw).
 */
QVariant actionColor(TGitPath::Actions actions)
{
    const auto *const found = std::ranges::find_if(actionColors, [actions](const ActionColor& entry) {
        return (actions & entry.actions) != 0;
    });
    return found == actionColors.cend() ? QVariant() : QColor::fromRgb(found->color);
}

/**
 * @param actions What happened to a file.
 * @returns Its group in a working tree's list (CGitStatusListCtrl::SetItemGroup).
 */
StatusGroup statusGroup(TGitPath::Actions actions)
{
    if (actions & (Action::SkipWorktree | Action::AssumeValid)) {
        return StatusGroup::IgnoreLocalChanges;
    }
    if (actions & Action::Ignore) {
        return StatusGroup::IgnoredFiles;
    }
    if (actions & Action::Unversioned) {
        return StatusGroup::NotVersionedFiles;
    }
    return StatusGroup::ModifiedFiles;
}

} // namespace

namespace kgt
{

GitStatusListModel::GitStatusListModel(QObject *parent)
    : QAbstractItemModel(parent)
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
            .parentNo = 0,
        };
        if (const NumStatEntry *stat = numStatByPath.value(statusEntry.path)) {
            entry.linesAdded = stat->added;
            entry.linesRemoved = stat->removed;
        }
        entries.append(std::move(entry));
    }
    return entries;
}

QList<GitStatusListEntry> GitStatusListModel::makeRevisionEntries(const QList<GitRevFiles>& files)
{
    QList<GitStatusListEntry> entries;
    for (qsizetype parentNo = 0; parentNo < files.size(); ++parentNo) {
        const GitRevFiles& diff = files.at(parentNo);
        QList<GitStatusListEntry> parentEntries = makeEntries(diff.status, diff.numStat);
        for (GitStatusListEntry& entry : parentEntries) {
            entry.parentNo = static_cast<int>(parentNo);
        }
        entries.append(parentEntries);
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

void GitStatusListModel::resetCheckStates(const QStringList& paths)
{
    for (const QString& path : paths) {
        m_checkedByPath.remove(path);
    }
}

GitStatusListModel::ShowFlags GitStatusListModel::showFlags() const
{
    return m_show;
}

const QList<GitStatusListEntry>& GitStatusListModel::allEntries() const
{
    return m_entries;
}

void GitStatusListModel::setParentHeaders(const QStringList& headers)
{
    beginResetModel();
    m_parentHeaders = headers;
    rebuildGroups();
    endResetModel();
}

bool GitStatusListModel::hasGroups() const
{
    return !m_groups.isEmpty();
}

int GitStatusListModel::listedCount() const
{
    return static_cast<int>(m_rows.size());
}

const GitStatusListEntry& GitStatusListModel::entry(int row) const
{
    Q_ASSERT(row >= 0 && row < m_rows.size());
    return m_entries.at(m_rows.at(row));
}

const GitStatusListEntry *GitStatusListModel::entry(const QModelIndex& index) const
{
    const qsizetype row = listedRow(index);
    return row < 0 ? nullptr : &m_entries.at(m_rows.at(row));
}

QModelIndex GitStatusListModel::entryIndex(int row, int column) const
{
    if (!hasGroups()) {
        return index(row, column);
    }
    for (int group = 0; group < m_groups.size(); ++group) {
        const qsizetype position = m_groups.at(group).rows.indexOf(row);
        if (position >= 0) {
            return index(static_cast<int>(position), column, index(group, 0));
        }
    }
    return {};
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
    emitCheckStatesChanged();
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

QModelIndex GitStatusListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return {};
    }
    // An entry under a group keeps the group's number plus one as its internal id; top-level rows keep 0
    return createIndex(row, column, parent.isValid() ? static_cast<quintptr>(parent.row()) + 1 : 0);
}

QModelIndex GitStatusListModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.internalId() == 0) {
        return {};
    }
    return createIndex(static_cast<int>(child.internalId() - 1), 0, quintptr(0));
}

int GitStatusListModel::rowCount(const QModelIndex& parent) const
{
    if (!hasGroups()) {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }
    if (!parent.isValid()) {
        return static_cast<int>(m_groups.size());
    }
    if (parent.internalId() != 0 || parent.column() != 0) {
        return 0;
    }
    return static_cast<int>(m_groups.at(parent.row()).rows.size());
}

int GitStatusListModel::columnCount(const QModelIndex& parent) const
{
    if (!parent.isValid() || (hasGroups() && parent.internalId() == 0)) {
        return ColumnCount;
    }
    return 0;
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
    const GitStatusListEntry *listEntry = entry(index);
    if (!listEntry) {
        return groupData(index, role);
    }
    const GitStatusListEntry& rowEntry = *listEntry;
    switch (role) {
    case Qt::DisplayRole:
        return displayData(rowEntry, index.column());
    case Qt::CheckStateRole:
        if (m_hasCheckboxes && index.column() == PathColumn) {
            return isChecked(rowEntry) ? Qt::Checked : Qt::Unchecked;
        }
        return {};
    case Qt::ForegroundRole:
        // CGitStatusListCtrl::OnNMCustomdraw's LOGACTIONS_GRAY
        if (rowEntry.isGrayed) {
            return QGuiApplication::palette().brush(QPalette::Disabled, QPalette::Text);
        }
        return actionColor(rowEntry.actions);
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
    const GitStatusListEntry *listEntry = entry(index);
    if (!listEntry) {
        return false;
    }
    const bool checked = value.value<Qt::CheckState>() == Qt::Checked;
    m_checkedByPath.insert(listEntry->path, checked);
    Q_EMIT dataChanged(index, index, {Qt::CheckStateRole});
    Q_EMIT checkedCountChanged();
    return true;
}

Qt::ItemFlags GitStatusListModel::flags(const QModelIndex& index) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return Qt::NoItemFlags;
    }
    if (!entry(index)) {
        // Not enabled either, so QTreeView's keyboard navigation steps over it, as it does a list view's group header
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

void GitStatusListModel::setDirectFiles(const QStringList& paths)
{
    m_directFiles = QSet<QString>(paths.cbegin(), paths.cend());
}

void GitStatusListModel::rebuildRows()
{
    m_rows.clear();
    const unsigned int show = m_show.toInt();
    const unsigned int check = m_check.toInt();
    for (qsizetype index = 0; index < m_entries.size(); ++index) {
        const GitStatusListEntry& listEntry = m_entries.at(index);
        const unsigned int actions = listEntry.actions.toInt();
        // A path seen for the first time gets its check state from the check flags, or is checked because the
        // user picked it directly (CGitStatusListCtrl::Show)
        if (!m_checkedByPath.contains(listEntry.path)) {
            const bool direct = m_show.testFlag(Show::DirectFiles) && m_directFiles.contains(listEntry.path);
            m_checkedByPath.insert(listEntry.path, (actions & check) != 0 || direct);
        }
        if ((actions & show) != 0) {
            m_rows.append(index);
        }
    }
    rebuildGroups();
}

void GitStatusListModel::rebuildGroups()
{
    // CGitStatusListCtrl::PrepareGroups. Its bForce is left out: nothing in TortoiseGit passes it
    m_groups.clear();
    int maxParent = 0;
    bool hasGroupedKind = false;
    const unsigned int groupedKinds =
        m_show.toInt() & TGitPath::Actions(Action::Unversioned | Action::Ignore | Action::AssumeValid | Action::SkipWorktree).toInt();
    for (const GitStatusListEntry& listEntry : std::as_const(m_entries)) {
        maxParent = std::max(maxParent, listEntry.parentNo);
        hasGroupedKind = hasGroupedKind || (listEntry.actions.toInt() & groupedKinds) != 0;
    }
    if (maxParent == 0 && !hasGroupedKind) {
        return;
    }

    QList<Group> groups = preparedGroups(maxParent);
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        // CGitStatusListCtrl::SetItemGroup. A commit's files are never unversioned or ignored, so a merge has only parents
        const GitStatusListEntry& listEntry = m_entries.at(m_rows.at(row));
        const qsizetype group = maxParent > 0 ? listEntry.parentNo : static_cast<qsizetype>(statusGroup(listEntry.actions));
        std::next(groups.begin(), group)->rows.append(row);
    }
    // A Windows list view doesn't show a group with no items
    std::ranges::copy_if(groups, std::back_inserter(m_groups), [](const Group& group) {
        return !group.rows.isEmpty();
    });
}

QList<GitStatusListModel::Group> GitStatusListModel::preparedGroups(int maxParent) const
{
    QList<Group> groups;
    if (maxParent > 0) {
        for (int parent = 0; parent < std::max<qsizetype>(maxParent + 1, m_parentHeaders.size()); ++parent) {
            groups.append({.header = parent < m_parentHeaders.size() ? m_parentHeaders.at(parent) : i18n("Diff with parent %1", parent + 1), .rows = {}});
        }
        return groups;
    }
    // In the order of StatusGroup
    for (const QString& header : {i18n("Modified Files"),
                                  i18n("Not Versioned Files"),
                                  i18n("Ignored Files"),
                                  i18n("Local changes ignored (assumed valid/unchanged or skip worktree flagged files)")}) {
        groups.append({.header = header, .rows = {}});
    }
    return groups;
}

void GitStatusListModel::emitCheckStatesChanged()
{
    if (!hasGroups()) {
        if (!m_rows.isEmpty()) {
            Q_EMIT dataChanged(index(0, PathColumn), index(rowCount() - 1, PathColumn), {Qt::CheckStateRole});
        }
        return;
    }
    for (int group = 0; group < m_groups.size(); ++group) {
        const QModelIndex groupIndex = index(group, 0);
        const int count = rowCount(groupIndex);
        if (count > 0) {
            Q_EMIT dataChanged(index(0, PathColumn, groupIndex), index(count - 1, PathColumn, groupIndex), {Qt::CheckStateRole});
        }
    }
}

qsizetype GitStatusListModel::listedRow(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return -1;
    }
    if (!hasGroups()) {
        return index.row();
    }
    if (index.internalId() == 0) {
        return -1;
    }
    return m_groups.at(static_cast<qsizetype>(index.internalId() - 1)).rows.at(index.row());
}

QVariant GitStatusListModel::groupData(const QModelIndex& index, int role) const
{
    if (index.column() != PathColumn) {
        return {};
    }
    switch (role) {
    case Qt::DisplayRole:
        return m_groups.at(index.row()).header;
    case Qt::FontRole: {
        QFont font;
        font.setBold(true);
        return font;
    }
    case Qt::ForegroundRole:
        // A header row isn't enabled, and that shouldn't gray it out
        return QGuiApplication::palette().brush(QPalette::Text);
    default:
        return {};
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
