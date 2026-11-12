// SPDX-FileCopyrightText: 2003-2008, 2018 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/FileDiffDlg.cpp

#include "filedifflistmodel.hpp"

#include "git/gitrevloglist.hpp"
#include "gitstatuslistmodel.hpp"
#include "logdlgfilefilter.hpp"

#include <KLocalizedString>

#include <QIcon>
#include <QMimeDatabase>

#include <algorithm>

namespace
{

using namespace kgt;

/**
 * @param text A line count from --numstat.
 * @returns It as a number, as CSorter::A2L reads it: 0 for "-" or nothing.
 */
long long lineCount(const QString& text)
{
    bool isNumber = false;
    const long long count = text.toLongLong(&isNumber);
    return isNumber ? count : 0;
}

/**
 * @param left A number.
 * @param right Another.
 * @returns Negative, zero or positive as @p left is less than, equal to or greater than @p right.
 */
int compareNumbers(long long left, long long right)
{
    return (left > right) - (left < right);
}

/**
 * The comparison of CFileDiffDlg::SortCompare before the path breaks a tie.
 * @param left An entry.
 * @param right Another.
 * @param column The column sorted by.
 * @returns Negative, zero or positive.
 */
int compareColumn(const FileDiffEntry& left, const FileDiffEntry& right, int column)
{
    switch (column) {
    case FileDiffListModel::FileColumn:
        return left.path.compare(right.path);
    case FileDiffListModel::ExtensionColumn:
        return FileDiffListModel::fileExtension(left).compare(FileDiffListModel::fileExtension(right));
    case FileDiffListModel::ActionColumn:
        return compareNumbers(left.actions.toInt(), right.actions.toInt());
    case FileDiffListModel::LinesAddedColumn:
        return compareNumbers(lineCount(left.linesAdded), lineCount(right.linesAdded));
    case FileDiffListModel::LinesRemovedColumn:
        return compareNumbers(lineCount(left.linesRemoved), lineCount(right.linesRemoved));
    default:
        return 0;
    }
}

} // namespace

namespace kgt
{

FileDiffListModel::FileDiffListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

QList<FileDiffEntry> FileDiffListModel::makeEntries(const GitRevFiles& files)
{
    QList<FileDiffEntry> entries;
    const QList<GitStatusListEntry> listEntries = GitStatusListModel::makeEntries(files.status, files.numStat);
    entries.reserve(listEntries.size());
    for (const GitStatusListEntry& listEntry : listEntries) {
        entries.append({
            .path = listEntry.path,
            .oldPath = listEntry.origPath,
            .actions = listEntry.actions,
            .linesAdded = listEntry.linesAdded,
            .linesRemoved = listEntry.linesRemoved,
            .isDirectory = false,
        });
    }
    return entries;
}

QString FileDiffListModel::fileName(const FileDiffEntry& entry)
{
    // Similar to the status list (CGitStatusListCtrl::GetCellText). AbbreviateRenamings isn't ported
    if (!(entry.actions & (TGitPath::Action::Replaced | TGitPath::Action::Copy)) || entry.oldPath.isEmpty()) {
        return entry.path;
    }
    return entry.path + QLatin1Char(' ') + i18n("(from %1)", entry.oldPath);
}

QString FileDiffListModel::fileExtension(const FileDiffEntry& entry)
{
    if (entry.isDirectory) {
        return {};
    }
    const qsizetype dotPos = entry.path.lastIndexOf(QLatin1Char('.'));
    const qsizetype slashPos = entry.path.lastIndexOf(QLatin1Char('/'));
    return dotPos > slashPos ? entry.path.mid(dotPos) : QString();
}

bool FileDiffListModel::sortCompare(const FileDiffEntry& lhs, const FileDiffEntry& rhs, int column, bool upward)
{
    int result = compareColumn(lhs, rhs, column);
    // Sort by path name as second priority
    if (column != FileColumn && result == 0) {
        result = lhs.path.compare(rhs.path);
    }
    if (!upward) {
        result = -result;
    }
    return result < 0;
}

void FileDiffListModel::sort(QList<FileDiffEntry>& entries, int column, bool ascending)
{
    std::ranges::sort(entries, [column, ascending](const FileDiffEntry& left, const FileDiffEntry& right) {
        return sortCompare(left, right, column, ascending);
    });
}

QList<FileDiffEntry> FileDiffListModel::filtered(const QList<FileDiffEntry>& entries, const QString& filterText)
{
    // CLogDlgFileFilter(sFilterText, 0, 0, false): words, case-insensitive
    const LogDlgFileFilter filter(filterText, false, 0, false);
    QList<FileDiffEntry> shown;
    for (const FileDiffEntry& entry : entries) {
        const bool hasStat = !entry.linesAdded.isEmpty() || !entry.linesRemoved.isEmpty();
        if (filter(entry.path, entry.oldPath) && (entry.isDirectory || hasStat)) {
            shown.append(entry);
        }
    }
    return shown;
}

void FileDiffListModel::setEntries(const QList<FileDiffEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

const QList<FileDiffEntry>& FileDiffListModel::entries() const
{
    return m_entries;
}

int FileDiffListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int FileDiffListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FileDiffListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const FileDiffEntry& entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case FileColumn:
            return fileName(entry);
        case ExtensionColumn:
            return fileExtension(entry);
        case ActionColumn:
            return GitStatusListModel::actionName(entry.actions);
        case LinesAddedColumn:
            return entry.linesAdded;
        case LinesRemovedColumn:
            return entry.linesRemoved;
        default:
            return {};
        }
    case Qt::ForegroundRole:
        return GitStatusListModel::actionColor(entry.actions);
    case Qt::DecorationRole:
        if (index.column() != FileColumn) {
            return {};
        }
        // TortoiseGit shows the system icon for the file type (AddEntry)
        if (entry.isDirectory) {
            return QIcon::fromTheme(QStringLiteral("folder"));
        }
        return QIcon::fromTheme(QMimeDatabase().mimeTypeForFile(entry.path, QMimeDatabase::MatchExtension).iconName());
    case Qt::ToolTipRole:
        // ShowListFullPathTooltip, on by default
        return entry.path;
    default:
        return {};
    }
}

QVariant FileDiffListModel::headerData(int section, Qt::Orientation axis, int role) const
{
    if (axis != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case FileColumn:
        return i18n("File");
    case ExtensionColumn:
        return i18n("Extension");
    case ActionColumn:
        return i18n("Action");
    case LinesAddedColumn:
        return i18n("Lines added");
    case LinesRemovedColumn:
        return i18n("Lines removed");
    default:
        return {};
    }
}

} // namespace kgt
