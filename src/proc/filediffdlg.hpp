// SPDX-FileCopyrightText: 2003-2008, 2018 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/FileDiffDlg.h and FileDiffDlg.cpp (CFileDiffDlg) and its IDD_DIFFFILES
 * resource: the Changed Files dialog, which lists the files that differ between two versions.
 */

#pragma once

#include "filedifflistmodel.hpp"
#include "git/git.hpp"
#include "utils/result.hpp"

#include <QDateTime>
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <array>
#include <memory>

class QAction;
class QGroupBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QToolButton;
class QTreeView;

namespace kgt
{

class GitCall;

/**
 * One of the versions compared, the parts of GitRev the dialog uses.
 */
struct FileDiffRev {
    QString hash; // m_CommitHash: the full hash; empty for the working tree
    QString subject; // GetSubject(): "Working Tree" for the working tree
    QString authorName; // GetAuthorName()
    QDateTime authorDate; // GetAuthorDate()
    QDateTime committerDate; // GetCommitterDate()
    bool isCommit = true; // IsCommit(): false for a tree

    bool operator==(const FileDiffRev&) const = default;
};

/**
 * The Diff Options menu's choices.
 */
struct FileDiffOptions {
    bool ignoreSpaceAtEol = false; // m_bIgnoreSpaceAtEol
    bool ignoreSpaceChange = false; // m_bIgnoreSpaceChange
    bool ignoreAllSpace = false; // m_bIgnoreAllSpace
    bool ignoreBlankLines = false; // m_bIgnoreBlankLines
    bool commonAncestor = false; // m_bCommonAncestorDiff: diff from the merge base (A...B)

    bool operator==(const FileDiffOptions&) const = default;
};

/**
 * What is compared: the versions' hashes and the Diff Options.
 */
struct FileDiffQuery {
    QString rev1; // The base's hash; empty for the working tree
    QString rev2; // Version 2's hash; empty for the working tree
    FileDiffOptions options; // The whitespace options; commonAncestor is applied to rev1 already
};

/**
 * Shows the files that differ between version 1, the base, and version 2, either of which may be the working tree.
 * The list loads in the background (DiffThread). Each version can be typed, with completion from the branches and
 * tags, or picked from Browse References, the log or the reflog. Double-clicking a file compares it in the diff
 * tool.
 */
class FileDiffDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param workingTree The working tree's root, or the bare repository.
     * @param parent Parent widget.
     */
    explicit FileDiffDlg(const QString& workingTree, QWidget *parent = nullptr);
    ~FileDiffDlg() override;

    FileDiffDlg(const FileDiffDlg&) = delete;
    FileDiffDlg& operator=(const FileDiffDlg&) = delete;
    FileDiffDlg(FileDiffDlg&&) = delete;
    FileDiffDlg& operator=(FileDiffDlg&&) = delete;

    /**
     * @returns How the working tree is named as a version: GitRev::GetWorkingCopyRef, forty zeros.
     */
    [[nodiscard]] static QString workingCopyRef();

    /**
     * The test GitRev::GetCommit makes.
     * @param text A version as typed.
     * @returns Whether it names the working tree: at least eight zeros, and no more than forty.
     */
    [[nodiscard]] static bool isWorkingCopyRef(const QString& text);

    /**
     * Ported from CGit::GetCommitDiffList.
     * @param query What to compare.
     * @returns The arguments after "git" for a diff whose output GitRevLoglist::parseFiles reads.
     */
    [[nodiscard]] static QStringList diffListArguments(const FileDiffQuery& query);

    /**
     * Ported from CFileDiffDlg::DoDiff: how a file is compared, left the base and right version 2. A file added
     * in the working tree is compared with nothing (CGitDiff::DiffNull). git difftool can't reverse a comparison
     * of two named blobs, so a rename against a working tree base shows the working tree on the right.
     * @param file The file.
     * @param query The versions; the options don't apply.
     * @param unborn Whether the branch has no commit yet (CGit::IsInitRepos).
     * @returns The arguments after "git" for git difftool.
     */
    [[nodiscard]] static QStringList diffArguments(const FileDiffEntry& file, const FileDiffQuery& query, bool unborn);

    /**
     * Ported from CFileDiffDlg::FillPatchView.
     * @param query What to compare.
     * @param selected The selected files, whose diffs are shown; with none, the whole diff with its stat.
     * @returns The arguments after "git" for the patch.
     */
    [[nodiscard]] static QStringList patchArguments(const FileDiffQuery& query, const QList<FileDiffEntry>& selected);

    /**
     * Ported from CFileDiffDlg::SetDiff: what to compare, as the user could type it. Loading starts at once.
     * @param path A folder to put into the filter box, relative to the working tree root (m_sFilter); empty for none.
     * @param baseRev1 Version 1, the base: a hash, ref or workingCopyRef().
     * @param rev2 Version 2.
     */
    void setDiff(const QString& path, const QString& baseRev1, const QString& rev2);

    /**
     * @returns Version 1 as last read.
     */
    [[nodiscard]] const FileDiffRev& rev1() const;

    /**
     * @returns Version 2 as last read.
     */
    [[nodiscard]] const FileDiffRev& rev2() const;

    /**
     * @returns Whether the list is loading.
     */
    [[nodiscard]] bool isLoading() const;

    /**
     * @returns The text written into the list in place of its entries (CHintCtrl::ShowText); empty for none.
     */
    [[nodiscard]] const QString& hintText() const;

    /**
     * @returns Every changed file found, sorted, whether shown or not (m_arFileList).
     */
    [[nodiscard]] const QList<FileDiffEntry>& allEntries() const;

    /**
     * @returns The file list.
     */
    [[nodiscard]] QTreeView *fileList() const;

    /**
     * @returns The model of the files shown (m_arFilteredList).
     */
    [[nodiscard]] FileDiffListModel *listModel() const;

    /**
     * @param index 0 for version 1, 1 for version 2.
     * @returns The version's edit box (IDC_REV1EDIT, IDC_REV2EDIT).
     */
    [[nodiscard]] QLineEdit *revEdit(int index) const;

    /**
     * @returns The filter box (IDC_FILTER).
     */
    [[nodiscard]] QLineEdit *filterEdit() const;

    /**
     * @returns The switch left/right button (IDC_SWITCHLEFTRIGHT).
     */
    [[nodiscard]] QToolButton *switchButton() const;

    /**
     * @returns The Diff Options menu.
     */
    [[nodiscard]] QMenu *diffOptionsMenu() const;

    /**
     * @returns The patch view, hidden until View Patch shows it.
     */
    [[nodiscard]] QPlainTextEdit *patchView() const;

    /**
     * Ported from CFileDiffDlg::OnBnClickedSwitchleftright: swaps the versions and loads the list again.
     */
    void switchLeftRight();

    /**
     * Saves the window size (EnableSaveRestore).
     * @param result The dialog's result.
     */
    void done(int result) override;

Q_SIGNALS:
    /**
     * The list has been loaded or has failed to load (WM_DIFFFINISHED).
     */
    void diffFinished();

protected:
    /**
     * Writes the hint into the empty list, and handles PreTranslateMessage's keys.
     * @param watched The object.
     * @param event The event.
     * @returns Whether the event was handled.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static constexpr unsigned int bothSides = 0x3; // SetURLLabels' mask for both versions

    /**
     * The controls and state of one version: its group box, edit box, HEAD button and label.
     */
    struct RevSide {
        QGroupBox *group = nullptr; // IDC_REV1GROUP, IDC_REV2GROUP
        QLineEdit *edit = nullptr; // IDC_REV1EDIT, IDC_REV2EDIT
        QToolButton *button = nullptr; // IDC_REV1BTN, IDC_REV2BTN
        QLineEdit *label = nullptr; // IDC_FIRSTURL, IDC_SECONDURL
        FileDiffRev rev; // m_rev1, m_rev2
        int buttonEntry = 0; // The CMenuButton's current entry: Reference Browser, Log or RefLog
    };

    // filediffdlg.cpp
    void createWidgets();
    QLayout *createTopRow();
    void createRevSide(int index, const QString& title);
    void createDiffOptionsMenu();
    [[nodiscard]] Result<FileDiffRev> readRev(const QString& text) const;
    bool fillRevFromString(FileDiffRev& rev, const QString& text);
    void startDiff();
    void onDiffFinished();
    void showHint(const QString& text);
    void setUrlLabels(unsigned int mask = bothSides);
    void clearUrlLabels(unsigned int mask);
    void filter();
    void enableInputControl(bool enable);
    void onInputTimer();
    void onRevEdited(int index);
    void clickRevButton(int index);
    void onHeaderClicked(int section);
    void onDiffOptionTriggered();
    void onShowLog();
    void loadRefs();
    void onRefsLoaded();
    void togglePatchView();
    void fillPatchView(bool onlySetTimer = false);
    void onPatchFinished();
    [[nodiscard]] bool isBothCommits() const;

    // filediffdlg_menu.cpp
    void showContextMenu(const QPoint& pos);
    void addRevertEntries(QMenu& menu, const FileDiffEntry& first);
    void doDiff(const FileDiffEntry& entry);
    [[nodiscard]] bool checkMultipleDiffs() const;
    void compareSelected();
    void unifiedDiffSelected();
    void revertSelectedItemToVersion(bool isOldVersion);
    void revertOne(const FileDiffEntry& entry, const QString& rev, bool isOldVersion) const;
    void showLogSelected(bool submodule);
    [[nodiscard]] QStringList submoduleRevArguments(const FileDiffEntry& entry) const;
    void saveList();
    void copySelectionToClipboard(bool isFull);
    void exportSelected();
    bool exportOne(const FileDiffEntry& entry);
    [[nodiscard]] QList<FileDiffEntry> selectedEntries() const;
    [[nodiscard]] QString absolutePath(const QString& path) const;

    Git m_git;
    QString m_workingTree;
    std::array<RevSide, 2> m_sides;
    QLabel *m_differenceLabel = nullptr; // IDC_DIFFSTATIC1
    QPushButton *m_diffOptions = nullptr; // IDC_DIFFOPTION
    QMenu *m_diffOptionsMenu = nullptr;
    std::array<QAction *, 5> m_diffOptionActions{}; // DIFFOPTION_*, in FileDiffOptions' order
    QPushButton *m_log = nullptr; // IDC_LOG
    QToolButton *m_switch = nullptr; // IDC_SWITCHLEFTRIGHT
    QLineEdit *m_filter = nullptr; // IDC_FILTER
    QSplitter *m_splitter = nullptr;
    QTreeView *m_fileList = nullptr; // IDC_FILELIST
    FileDiffListModel *m_model = nullptr;
    QPlainTextEdit *m_patchView = nullptr; // m_patchViewdlg
    QLabel *m_showPatch = nullptr; // IDC_VIEW_PATCH
    QList<FileDiffEntry> m_entries; // m_arFileList, sorted
    QString m_hint; // The text CHintCtrl::ShowText writes into the list
    QString m_exportDir; // m_strExportDir
    FileDiffOptions m_options;
    std::unique_ptr<GitCall> m_diffCall; // The running DiffThread
    std::unique_ptr<GitCall> m_patchCall; // The running FillPatchView
    std::unique_ptr<GitCall> m_branchCall; // LoadRefThread's branches
    std::unique_ptr<GitCall> m_tagCall; // LoadRefThread's tags
    QTimer m_filterTimer; // IDT_FILTER
    QTimer m_inputTimer; // IDT_INPUT
    QTimer m_patchTimer; // IDT_FILLPATCHVTIMER
    int m_sortedColumn = -1; // m_nSortedColumn
    bool m_ascending = true; // m_bAscending
    bool m_isBare = false; // m_bIsBare
};

} // namespace kgt
