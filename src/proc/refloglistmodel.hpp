// SPDX-FileCopyrightText: 2009-2011, 2015-2017, 2020-2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from the columns of TortoiseGit src/TortoiseProc/refloglist.h (CRefLogList): the table behind RefLogList.
 */

#pragma once

#include "git/gitrevloglist.hpp"

#include <QAbstractTableModel>
#include <QList>

namespace kgt
{

/**
 * The entries of one ref's reflog, newest first, in CRefLogList's columns.
 */
class RefLogListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /**
     * The columns, in TortoiseGit's order (REFLOG_*).
     */
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): Qt's model API takes columns as int
    enum Column {
        HashColumn, // REFLOG_HASH
        RefColumn, // REFLOG_REF
        ActionColumn, // REFLOG_ACTION
        MessageColumn, // REFLOG_MESSAGE
        DateColumn, // REFLOG_DATE
        ColumnCount,
    };

    /**
     * @param parent Parent object.
     */
    explicit RefLogListModel(QObject *parent = nullptr);

    /**
     * @param entries What to show, newest first.
     */
    void setEntries(const QList<GitRevLoglist>& entries);

    /**
     * @returns What is shown, newest first (CRefLogList's m_arShownList).
     */
    [[nodiscard]] const QList<GitRevLoglist>& entries() const;

    /**
     * @param parent Must be invalid: the model is a flat table.
     * @returns The number of entries.
     */
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;

    /**
     * @param parent Must be invalid: the model is a flat table.
     * @returns The number of columns.
     */
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;

    /**
     * Ported from CRefLogList::OnLvnGetdispinfoLoglist.
     * @param index The cell.
     * @param role The role.
     * @returns The cell's text for Qt::DisplayRole.
     */
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @param section The column.
     * @param axis The header.
     * @param role The role.
     * @returns The column's title, InsertRefLogColumn's strings.
     */
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation axis, int role = Qt::DisplayRole) const override;

private:
    QList<GitRevLoglist> m_entries;
};

} // namespace kgt
