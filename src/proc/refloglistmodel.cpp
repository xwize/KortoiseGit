// SPDX-FileCopyrightText: 2009-2011, 2013, 2015-2020, 2023-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/refloglist.cpp (InsertRefLogColumn and OnLvnGetdispinfoLoglist).
 */

#include "refloglistmodel.hpp"

#include "gitloglistmodel.hpp"

#include <KLocalizedString>

namespace kgt
{

RefLogListModel::RefLogListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void RefLogListModel::setEntries(const QList<GitRevLoglist>& entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

const QList<GitRevLoglist>& RefLogListModel::entries() const
{
    return m_entries;
}

int RefLogListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int RefLogListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant RefLogListModel::data(const QModelIndex& index, int role) const
{
    if (role != Qt::DisplayRole || !index.isValid()) {
        return {};
    }
    const GitRevLoglist& entry = m_entries.at(index.row());
    switch (index.column()) {
    case HashColumn:
        return entry.hash;
    case RefColumn:
        return entry.ref;
    case ActionColumn:
        return entry.refAction;
    case MessageColumn:
        return entry.subject.trimmed();
    case DateColumn:
        return GitLogListModel::formatDate(entry.commitDate);
    default:
        return {};
    }
}

QVariant RefLogListModel::headerData(int section, Qt::Orientation axis, int role) const
{
    if (axis != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case HashColumn:
        return i18n("SHA-1");
    case RefColumn:
        return i18n("Ref");
    case ActionColumn:
        return i18n("Action");
    case MessageColumn:
        return i18n("Message");
    case DateColumn:
        return i18n("Date");
    default:
        return {};
    }
}

} // namespace kgt
