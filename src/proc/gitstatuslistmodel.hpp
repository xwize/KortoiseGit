// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * The data half of TortoiseGit src/Git/GitStatusListCtrl.h (CGitStatusListCtrl): its entries, their check
 * state, the show flags (GITSLC_SHOW*) that pick which of them are listed, and the cell texts of its columns.
 * GitStatusListCtrl is the view over it.
 */

#pragma once

#include "git/gitnumstat.hpp"
#include "git/gitrevloglist.hpp"
#include "git/gitstatus.hpp"
#include "git/tgitpath.hpp"

#include <QAbstractItemModel>
#include <QFlags>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace kgt
{

/**
 * One file the status list knows about, listed or not: its status and its line counts against HEAD.
 */
struct GitStatusListEntry {
    QString path; // Relative to the working tree root, '/'-separated
    QString origPath; // The rename or copy source; empty otherwise
    TGitPath::Actions actions; // TortoiseGit's m_Action: what happened to the file, Missing included
    QString linesAdded; // TortoiseGit's m_StatAdd: a number, "-" for a binary file, empty if unknown
    QString linesRemoved; // TortoiseGit's m_StatDel
    int parentNo = 0; // TortoiseGit's m_ParentNo: in a commit's list, the parent the change is against
    bool isGrayed = false; // TortoiseGit's LOGACTIONS_GRAY: an unrelated path in a log limited to a path

    bool operator==(const GitStatusListEntry&) const = default;
};

/**
 * The status list's rows: the entries the show flags let through, each with a checkbox. The check state of every
 * path is remembered across reloads and changes of the show flags, as TortoiseGit's m_mapFilenameToChecked does.
 *
 * The list is flat until CGitStatusListCtrl::PrepareGroups' conditions hold: unversioned, ignored or assume-valid /
 * skip-worktree files are shown and there are some, or a merge commit's files are compared with more than one
 * parent. Then the entries are the children of header rows, one per group, and groups with no entries have no row,
 * as a Windows list view hides them. TortoiseGit's changelist groups are not ported.
 */
class GitStatusListModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    /**
     * The columns, in TortoiseGit's order (eCol). TortoiseGit's optional Filename, modification date, size and LFS
     * lock columns are not ported yet.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): Qt's model API takes columns as int
    enum Column {
        PathColumn, // eCol_Name: "Path", with the checkbox
        ExtensionColumn, // eCol_Extension
        StatusColumn, // eCol_Status
        LinesAddedColumn, // eCol_Add
        LinesRemovedColumn, // eCol_Del
        ColumnCount,
    };

    /**
     * Extra item data roles.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): Qt's model API takes roles as int
    enum Role {
        SortRole = Qt::UserRole + 1, // What a column sorts by: numbers for the line counts, text otherwise
        PathRole, // The entry's path, relative to the working tree root
        ActionsRole, // The entry's TGitPath::Actions, as unsigned int
    };

    /**
     * Which kinds of entries are listed (TortoiseGit's GITSLC_SHOW*, with the same values). An entry is listed if
     * any of its actions is in the flags. TortoiseGit's flags that are zero (normal, missing, obstructed, ...) and
     * those for features not ported yet (direct files, LFS locks, submodules) are left out.
     */
    enum class Show : unsigned int {
        Unversioned = 0x80000000, // GITSLC_SHOWUNVERSIONED
        Modified = 0x00000002, // GITSLC_SHOWMODIFIED
        Added = 0x00000041, // GITSLC_SHOWADDED: added or copied
        Removed = 0x00000008, // GITSLC_SHOWREMOVED, which takes in missing files too
        Conflicted = 0x00000010, // GITSLC_SHOWCONFLICTED
        Replaced = 0x00000004, // GITSLC_SHOWREPLACED: renamed
        Merged = 0x00000080, // GITSLC_SHOWMERGED
        Ignored = 0x40000000, // GITSLC_SHOWIGNORED
        AssumeValid = 0x00000200, // GITSLC_SHOWASSUMEVALID
        SkipWorktree = 0x00000400, // GITSLC_SHOWSKIPWORKTREE
        DirectFiles = 0x04000000, // GITSLC_SHOWDIRECTFILES: the files set with setDirectFiles() start checked
        /**
         * GITSLC_SHOWVERSIONED. TortoiseGit's also holds GITSLC_SHOWIGNORED, to no effect: it loads ignored files
         * only when a dialog asks for them separately. Here asking to show them is that request, so it is left out.
         */
        Versioned = Modified | Added | Removed | Conflicted | Replaced | Merged,
        VersionedButNormal = Versioned, // GITSLC_SHOWVERSIONEDBUTNORMAL: git status never lists unchanged files
        All = Versioned | Unversioned, // GITSLC_SHOWALL
    };
    Q_DECLARE_FLAGS(ShowFlags, Show)

    /**
     * @param parent The owning object.
     */
    explicit GitStatusListModel(QObject *parent = nullptr);

    /**
     * Combines a status with its line counts into the list's entries, as CGit::GetWorkingTreeChanges does: a file
     * deleted from the working tree is also Missing.
     * @param status The working tree's status.
     * @param numStat The line counts against HEAD, matched to the status by path.
     * @returns An entry per status entry, in the same order.
     */
    [[nodiscard]] static QList<GitStatusListEntry> makeEntries(const QList<GitStatusEntry>& status, const QList<NumStatEntry>& numStat);

    /**
     * The entries of a commit's file list, as the log shows them: the changes against each parent, each entry
     * marked with its parent (TortoiseGit's m_ParentNo).
     * @param files A commit's files, one GitRevFiles per parent.
     * @returns The entries, parent by parent.
     */
    [[nodiscard]] static QList<GitStatusListEntry> makeRevisionEntries(const QList<GitRevFiles>& files);

    /**
     * Replaces every entry, keeping the remembered check state of paths seen before.
     * @param entries All entries, listed or not.
     */
    void setEntries(const QList<GitStatusListEntry>& entries);

    /**
     * Picks which entries are listed and which new ones start checked (CGitStatusListCtrl::Show).
     * @param show The kinds of entries to list.
     * @param check The kinds of entries that start checked the first time they are seen. TortoiseGit's commit
     *              dialog checks tracked changes and leaves unversioned files unchecked.
     */
    void setShowFlags(ShowFlags show, ShowFlags check);

    /**
     * Names the files the user picked directly, rather than through a folder (CGitStatusListCtrl::m_setDirectFiles).
     * They start checked when the show flags hold Show::DirectFiles.
     * @param paths Paths relative to the working tree root.
     */
    void setDirectFiles(const QStringList& paths);

    /**
     * Forgets the remembered check state of some paths, so the next setEntries() gives them the default for their
     * status (m_mapFilenameToChecked.erase).
     * @param paths Paths relative to the working tree root.
     */
    void resetCheckStates(const QStringList& paths);

    /**
     * @returns The kinds of entries that are listed.
     */
    [[nodiscard]] ShowFlags showFlags() const;

    /**
     * @returns Every entry, listed or not, in the order given to setEntries().
     */
    [[nodiscard]] const QList<GitStatusListEntry>& allEntries() const;

    /**
     * Names the groups of a commit's files, one per parent (IDS_STATUSLIST_GROUP_DIFFWITHPARENT). They are used
     * only when an entry is against a parent other than the first.
     * @param headers The header of each parent's group, in parent order; empty for no groups.
     */
    void setParentHeaders(const QStringList& headers);

    /**
     * @returns Whether the entries are grouped under header rows.
     */
    [[nodiscard]] bool hasGroups() const;

    /**
     * @returns How many entries are listed, not counting group headers.
     */
    [[nodiscard]] int listedCount() const;

    /**
     * @param row Which listed entry, from 0 to listedCount(); the same as the model row when there are no groups.
     * @returns The entry.
     */
    [[nodiscard]] const GitStatusListEntry& entry(int row) const;

    /**
     * @param index An index of this model.
     * @returns The entry shown there, or nullptr for a group header.
     */
    [[nodiscard]] const GitStatusListEntry *entry(const QModelIndex& index) const;

    /**
     * @param row Which listed entry, from 0 to listedCount().
     * @param column A column.
     * @returns The model index showing it, under its group header if the list is grouped.
     */
    [[nodiscard]] QModelIndex entryIndex(int row, int column = PathColumn) const;

    /**
     * @param entry An entry of this model.
     * @returns Whether it is checked.
     */
    [[nodiscard]] bool isChecked(const GitStatusListEntry& entry) const;

    /**
     * @returns The listed entries that are checked, in row order.
     */
    [[nodiscard]] QList<GitStatusListEntry> checkedEntries() const;

    /**
     * @returns How many listed entries are checked (TortoiseGit's m_nSelected).
     */
    [[nodiscard]] int checkedCount() const;

    /**
     * Checks or unchecks every listed entry (CGitStatusListCtrl::SelectAll).
     * @param checked The new state.
     */
    void setAllChecked(bool checked);

    /**
     * Turns the checkboxes on or off (the bHasCheckboxes argument of CGitStatusListCtrl::Init). They are on by
     * default; the log dialog's list has none.
     * @param hasCheckboxes Whether the Path column has a checkbox.
     */
    void setHasCheckboxes(bool hasCheckboxes);

    /**
     * @returns Whether the Path column has a checkbox.
     */
    [[nodiscard]] bool hasCheckboxes() const;

    /**
     * @param actions What happened to a file.
     * @returns The Status column's text for it (CTGitPath::GetActionName).
     */
    [[nodiscard]] static QString actionName(TGitPath::Actions actions);

    /**
     * @param actions What happened to a file.
     * @returns The colour of its text, or an invalid QVariant for the normal one
     *          (CGitStatusListCtrl::OnNMCustomdraw).
     */
    [[nodiscard]] static QVariant actionColor(TGitPath::Actions actions);

    /**
     * @param path A path relative to the working tree root.
     * @returns The Extension column's text: from the last dot in the file name on, or empty
     *          (CTGitPath::GetFileExtension).
     */
    [[nodiscard]] static QString fileExtension(const QString& path);

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

Q_SIGNALS:
    /**
     * The number of checked listed entries, or the number of listed entries, may have changed.
     */
    void checkedCountChanged();

private:
    /**
     * A header row and the entries under it.
     */
    struct Group {
        QString header;
        QList<qsizetype> rows; // The index in m_rows of each entry
    };

    void rebuildRows();
    void rebuildGroups();
    void emitCheckStatesChanged();
    [[nodiscard]] qsizetype listedRow(const QModelIndex& index) const;
    [[nodiscard]] QVariant groupData(const QModelIndex& index, int role) const;
    [[nodiscard]] QList<Group> preparedGroups(int maxParent) const;
    [[nodiscard]] QVariant displayData(const GitStatusListEntry& entry, int column) const;
    [[nodiscard]] static QVariant sortData(const GitStatusListEntry& entry, int column);

    QList<GitStatusListEntry> m_entries;
    QList<qsizetype> m_rows; // TortoiseGit's m_arListArray: the index in m_entries of each listed row
    QStringList m_parentHeaders; // The header of each parent's group
    QList<Group> m_groups; // When grouped: the groups that have entries, in display order
    QHash<QString, bool> m_checkedByPath; // TortoiseGit's m_mapFilenameToChecked
    QSet<QString> m_directFiles; // TortoiseGit's m_setDirectFiles
    ShowFlags m_show;
    ShowFlags m_check;
    bool m_hasCheckboxes = true;
};

} // namespace kgt

Q_DECLARE_OPERATORS_FOR_FLAGS(kgt::GitStatusListModel::ShowFlags)
