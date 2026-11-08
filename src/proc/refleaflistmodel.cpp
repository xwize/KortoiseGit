// SPDX-FileCopyrightText: 2009-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/BrowseRefsDlg.cpp (FillListCtrlForShadowTree, CRefLeafListCompareFunc)

#include "refleaflistmodel.hpp"

#include "shadowtree.hpp"

#include <KLocalizedString>

#include <QCollator>
#include <QFont>
#include <QLocale>

#include <algorithm>

namespace
{

using namespace kgt;

/**
 * @param date A date.
 * @returns It as CLoglistUtils::FormatDateAndTime shows it with the short date format, or empty if unknown.
 */
QString formatDate(const QDateTime& date)
{
    return date.isValid() ? QLocale().toString(date, QLocale::ShortFormat) : QString();
}

/**
 * @param left A date.
 * @param right Another.
 * @returns -1, 0 or 1 as @p left is earlier, the same or later.
 */
int compareDates(const QDateTime& left, const QDateTime& right)
{
    if (left == right) {
        return 0;
    }
    return left > right ? 1 : -1;
}

/**
 * CRefLeafListCompareFunc::CompareNoDesc.
 * @param collator Compares text in natural order ignoring case, as StrCmpLogicalW.
 * @param column The sort column.
 * @param left A row.
 * @param right Another.
 * @returns Negative, zero or positive as @p left sorts before, with or after @p right.
 */
int compareNoDesc(const QCollator& collator, int column, const ShadowTree& left, const ShadowTree& right)
{
    switch (column) {
    case RefLeafListModel::NameColumn:
        return collator.compare(left.refName(), right.refName());
    case RefLeafListModel::UpstreamColumn:
        return collator.compare(left.ref.upstreamRef, right.ref.upstreamRef);
    case RefLeafListModel::AuthorDateColumn:
        return compareDates(left.ref.authorDate, right.ref.authorDate);
    case RefLeafListModel::MsgColumn:
        return collator.compare(left.ref.subject, right.ref.subject);
    case RefLeafListModel::LastAuthorColumn:
        return collator.compare(left.ref.authorName, right.ref.authorName);
    case RefLeafListModel::CommitterDateColumn:
        return compareDates(left.ref.committerDate, right.ref.committerDate);
    case RefLeafListModel::LastCommitterColumn:
        return collator.compare(left.ref.committerName, right.ref.committerName);
    case RefLeafListModel::HashColumn:
        return left.ref.hash.compare(right.ref.hash, Qt::CaseInsensitive);
    case RefLeafListModel::DescriptionColumn:
        return collator.compare(left.ref.description, right.ref.description);
    default:
        return 0;
    }
}

/**
 * The Tracked branch column, as CBrowseRefsDlg::Refresh shortens it: refs/remotes/origin/main becomes
 * origin/main, and an upstream that is gone is marked so.
 * @param tree The ref's leaf.
 * @returns The text.
 */
QString upstreamText(const ShadowTree& tree)
{
    QString upstream = tree.ref.upstreamRef;
    for (const QLatin1StringView prefix : {QLatin1StringView("refs/"), QLatin1StringView("remotes/")}) {
        if (upstream.startsWith(prefix)) {
            upstream = upstream.mid(prefix.size());
        }
    }
    if (!upstream.isEmpty() && tree.ref.upstreamGone) {
        return QStringLiteral("(gone: ") + upstream + QLatin1Char(')');
    }
    return upstream;
}

/**
 * @param entry A row.
 * @param column A column.
 * @returns The cell's text, as FillListCtrlForShadowTree sets it.
 */
QString cellText(const RefLeafListEntry& entry, int column)
{
    const GitRevRefBrowser& ref = entry.tree->ref;
    switch (column) {
    case RefLeafListModel::NameColumn:
        return entry.name;
    case RefLeafListModel::UpstreamColumn:
        return upstreamText(*entry.tree);
    case RefLeafListModel::AuthorDateColumn:
        return formatDate(ref.authorDate);
    case RefLeafListModel::MsgColumn:
        return ref.subject;
    case RefLeafListModel::LastAuthorColumn:
        return ref.authorName;
    case RefLeafListModel::CommitterDateColumn:
        return formatDate(ref.committerDate);
    case RefLeafListModel::LastCommitterColumn:
        return ref.committerName;
    case RefLeafListModel::HashColumn:
        return ref.hash;
    case RefLeafListModel::DescriptionColumn:
        return QString(ref.description).replace(QLatin1Char('\n'), QLatin1Char(' '));
    default:
        return {};
    }
}

} // namespace

namespace kgt
{

RefLeafListModel::RefLeafListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void RefLeafListModel::setEntries(const QList<RefLeafListEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
    sort(m_sortColumn, m_sortOrder);
}

const RefLeafListEntry& RefLeafListModel::entry(int row) const
{
    Q_ASSERT(row >= 0 && row < m_entries.size());
    return m_entries.at(row);
}

void RefLeafListModel::setCurrentBranchRef(const QString& refName)
{
    m_currentBranchRef = refName;
}

int RefLeafListModel::sortColumn() const
{
    return m_sortColumn;
}

Qt::SortOrder RefLeafListModel::sortOrder() const
{
    return m_sortOrder;
}

int RefLeafListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int RefLeafListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant RefLeafListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) {
        return {};
    }
    const RefLeafListEntry& row = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return cellText(row, index.column());
    case Qt::FontRole:
        // The current branch is bold, as TortoiseGit's list draws it
        if (!m_currentBranchRef.isEmpty() && row.tree->refName() == m_currentBranchRef) {
            QFont font;
            font.setBold(true);
            return font;
        }
        return {};
    default:
        return {};
    }
}

QVariant RefLeafListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case NameColumn:
        return i18n("Branch Name");
    case UpstreamColumn:
        return i18n("Tracked branch");
    case AuthorDateColumn:
        return i18n("Last Author Date");
    case MsgColumn:
        return i18n("Last Commit");
    case LastAuthorColumn:
        return i18n("Last Author");
    case CommitterDateColumn:
        return i18n("Date Last Commit");
    case LastCommitterColumn:
        return i18n("Last Committer");
    case HashColumn:
        return i18n("SHA-1");
    case DescriptionColumn:
        return i18n("Description");
    default:
        return {};
    }
}

void RefLeafListModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    Q_EMIT layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    const QModelIndexList oldIndexes = persistentIndexList();
    QList<const ShadowTree *> oldTrees;
    oldTrees.reserve(oldIndexes.size());
    for (const QModelIndex& index : oldIndexes) {
        oldTrees.append(m_entries.at(index.row()).tree);
    }
    std::ranges::stable_sort(m_entries, [&](const RefLeafListEntry& left, const RefLeafListEntry& right) {
        const int result = compareNoDesc(collator, column, *left.tree, *right.tree);
        return order == Qt::AscendingOrder ? result < 0 : result > 0;
    });
    QModelIndexList newIndexes;
    newIndexes.reserve(oldIndexes.size());
    for (qsizetype i = 0; i < oldIndexes.size(); ++i) {
        const auto found = std::ranges::find(m_entries, oldTrees.at(i), &RefLeafListEntry::tree);
        newIndexes.append(index(static_cast<int>(found - m_entries.begin()), oldIndexes.at(i).column()));
    }
    changePersistentIndexList(oldIndexes, newIndexes);
    Q_EMIT layoutChanged({}, QAbstractItemModel::VerticalSortHint);
}

} // namespace kgt
