// SPDX-FileCopyrightText: 2003-2008, 2018 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/FileDiffDlg.cpp: the file list of CFileDiffDlg (m_cFileList,
 * m_arFileList and m_arFilteredList), with its columns, sort order and filter.
 */

#pragma once

#include "git/tgitpath.hpp"

#include <QAbstractTableModel>
#include <QList>
#include <QString>

namespace kgt
{

struct GitRevFiles;

/**
 * One file that differs between the two versions, CFileDiffDlg's CTGitPath entries.
 */
struct FileDiffEntry {
    QString path; // In version 2, relative to the working tree root
    QString oldPath; // In version 1 for a rename or copy (GetGitOldPathString); empty otherwise
    TGitPath::Actions actions; // m_Action
    QString linesAdded; // m_StatAdd: a number, "-" for a binary file, empty if git gave none
    QString linesRemoved; // m_StatDel
    bool isDirectory = false; // IsDirectory(): a submodule

    bool operator==(const FileDiffEntry&) const = default;
};

/**
 * The files shown: the entries the filter lets through, in the order they were sorted.
 */
class FileDiffListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /**
     * The columns, in CFileDiffDlg::OnInitDialog's order.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): Qt's model API takes columns as int
    enum Column {
        FileColumn, // IDS_FILEDIFF_FILE
        ExtensionColumn, // IDS_FILEDIFF_EXT
        ActionColumn, // IDS_FILEDIFF_ACTION
        LinesAddedColumn, // IDS_FILEDIFF_STATADD
        LinesRemovedColumn, // IDS_FILEDIFF_STATDEL
        ColumnCount,
    };

    /**
     * @param parent The owning object.
     */
    explicit FileDiffListModel(QObject *parent = nullptr);

    /**
     * Combines a diff's raw records with their line counts (CTGitPathList::ParserFromLog).
     * @param files The parsed output of git diff --raw --numstat.
     * @returns An entry per changed file, in git's order, none of them a directory yet.
     */
    [[nodiscard]] static QList<FileDiffEntry> makeEntries(const GitRevFiles& files);

    /**
     * Ported from GetFilename in FileDiffDlg.cpp.
     * @param entry A file.
     * @returns The File column's text: the path, followed by "(from <old path>)" for a rename or copy.
     */
    [[nodiscard]] static QString fileName(const FileDiffEntry& entry);

    /**
     * Ported from CTGitPath::GetFileExtension.
     * @param entry A file.
     * @returns The Extension column's text: from the last dot in the file name on, or empty.
     */
    [[nodiscard]] static QString fileExtension(const FileDiffEntry& entry);

    /**
     * Ported from CFileDiffDlg::SortCompare.
     * @param lhs An entry.
     * @param rhs Another entry.
     * @param column The column sorted by, or -1 when none is (then the path decides).
     * @param upward Whether the order is ascending.
     * @returns Whether @p lhs comes first.
     */
    [[nodiscard]] static bool sortCompare(const FileDiffEntry& lhs, const FileDiffEntry& rhs, int column, bool upward);

    /**
     * Ported from CFileDiffDlg::Sort.
     * @param[in,out] entries The entries to sort.
     * @param column The column sorted by, or -1.
     * @param ascending Whether the order is ascending.
     */
    static void sort(QList<FileDiffEntry>& entries, int column, bool ascending);

    /**
     * Ported from CFileDiffDlg::Filter: the entries whose paths match the filter text. An entry without line counts
     * is left out unless it is a directory: since Git 2.29, --numstat skips files whose changes are all ignored by
     * the diff options.
     * @param entries All the entries, sorted.
     * @param filterText The filter box's text.
     * @returns The entries to show, in the same order.
     */
    [[nodiscard]] static QList<FileDiffEntry> filtered(const QList<FileDiffEntry>& entries, const QString& filterText);

    /**
     * @param entries The entries to show.
     */
    void setEntries(const QList<FileDiffEntry>& entries);

    /**
     * @returns The entries shown, by row.
     */
    [[nodiscard]] const QList<FileDiffEntry>& entries() const;

    /**
     * @param parent Must be invalid: the list is flat.
     * @returns The number of entries shown.
     */
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;

    /**
     * @param parent Must be invalid.
     * @returns ColumnCount.
     */
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;

    /**
     * The cells' text, with the colours of CFileDiffDlg::OnNMCustomdrawFilelist and the full path as the tooltip
     * (OnLvnGetInfoTipFilelist).
     * @param index A cell.
     * @param role The data role.
     * @returns The data, or an invalid QVariant.
     */
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /**
     * @param section The column.
     * @param axis Only horizontal has headers.
     * @param role The data role.
     * @returns The column's title.
     */
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation axis, int role = Qt::DisplayRole) const override;

private:
    QList<FileDiffEntry> m_entries;
};

} // namespace kgt
