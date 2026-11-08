// SPDX-FileCopyrightText: 2009-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * The model behind the ref browser's list, TortoiseGit's m_ListRefLeafs in src/TortoiseProc/BrowseRefsDlg.cpp, with
 * its columns (CBrowseRefsDlg::eCol) and its sorting (CRefLeafListCompareFunc).
 */

#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QString>

namespace kgt
{

struct ShadowTree;

/**
 * One row of the ref list: a leaf of the ref tree and the name shown for it, relative to the selected tree node.
 */
struct RefLeafListEntry {
    const ShadowTree *tree = nullptr; // The ref's leaf, owned by the dialog's tree
    QString name; // The name shown, such as main or feature/x
};

/**
 * The refs under the selected tree node, one per row.
 */
class RefLeafListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): Qt's model API takes columns as int
    enum Column {
        NameColumn, // eCol_Name
        UpstreamColumn, // eCol_Upstream
        AuthorDateColumn, // eCol_AuthorDate
        MsgColumn, // eCol_Msg
        LastAuthorColumn, // eCol_LastAuthor
        CommitterDateColumn, // eCol_CommitterDate
        LastCommitterColumn, // eCol_LastCommitter
        HashColumn, // eCol_Hash
        DescriptionColumn, // eCol_Description
        ColumnCount,
    };

    /**
     * @param parent The owning object.
     */
    explicit RefLeafListModel(QObject *parent = nullptr);

    /**
     * Replaces the rows and sorts them by the current sort column.
     * @param entries The rows.
     */
    void setEntries(const QList<RefLeafListEntry>& entries);

    /**
     * @param row A row.
     * @returns Its entry.
     */
    [[nodiscard]] const RefLeafListEntry& entry(int row) const;

    /**
     * @param refName The full name of the current branch, such as refs/heads/main, shown bold; empty for none.
     */
    void setCurrentBranchRef(const QString& refName);

    /**
     * @returns The column the rows are sorted by.
     */
    [[nodiscard]] int sortColumn() const;

    /**
     * @returns The order the rows are sorted in.
     */
    [[nodiscard]] Qt::SortOrder sortOrder() const;

    /**
     * @param parent Must be invalid: the model is flat.
     * @returns The number of refs.
     */
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;

    /**
     * @param parent Must be invalid: the model is flat.
     * @returns ColumnCount.
     */
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;

    /**
     * @param index A cell.
     * @param role The role.
     * @returns The cell's text, or its font (bold for the current branch).
     */
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @param section A column.
     * @param orientation Horizontal for the column titles.
     * @param role The role.
     * @returns TortoiseGit's column title.
     */
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /**
     * Sorts as CRefLeafListCompareFunc: names in natural order ignoring case, dates by time.
     * @param column The column.
     * @param order The order.
     */
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    QList<RefLeafListEntry> m_entries;
    QString m_currentBranchRef;
    int m_sortColumn = NameColumn;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

} // namespace kgt
