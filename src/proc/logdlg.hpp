// SPDX-FileCopyrightText: 2003-2009, 2015 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/LogDlg.h (CLogDlg): the log dialog: the date range and search box, the
 * commit list, the selected commit's message and its changed files, and the Refresh, Walk Behavior and View buttons.
 */

#pragma once

#include "git/gitrevloglist.hpp"
#include "utils/urlfinder.hpp"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QTimer>

class QAction;
class QDateEdit;
class QDialogButtonBox;
class QLineEdit;
class QMenu;
class QPushButton;
class QTextBrowser;
class QUrl;

namespace kgt
{

class GitLogList;
class GitStatusListCtrl;
struct GitStatusListEntry;

/**
 * Ported from LookLikeGitHash: reads the run of lowercase hex digits starting at pos.
 * @param msg The text.
 * @param pos Where the run starts; moved to the first character after it.
 * @returns Whether the run is long enough for a (short) hash and not followed by '@'.
 */
[[nodiscard]] bool lookLikeGitHash(const QString& msg, qsizetype& pos);

/**
 * Ported from FindGitHashPositions: the words in msg that look like commit hashes, from offset on, leaving out
 * git-svn-id:, Signed-off-by: and Change-Id: lines. A "g" in front of the hex digits, as git describe writes, is
 * not part of the range.
 * @returns The hashes' ranges, in order.
 */
[[nodiscard]] QList<TextRange> findGitHashPositions(const QString& msg, qsizetype offset);

/**
 * The log dialog: the history of HEAD, limited to the paths it was opened for when they are a file or a
 * subfolder. Selecting one commit shows its full message and the files it changed against each of its parents;
 * double-clicking a file compares it in git difftool. As in TortoiseGit, every change of the walk, the dates or the
 * filter runs the log again in the background.
 */
class LogDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Opens the dialog and starts loading the log.
     * @param paths The absolute paths the log was asked for, all in one repository.
     * @param range The revisions to list (TortoiseGit's /range), or empty for HEAD's history.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit LogDlg(const QStringList& paths, const QString& range = {}, QWidget *parent = nullptr);
    ~LogDlg() override;

    LogDlg(const LogDlg&) = delete;
    LogDlg& operator=(const LogDlg&) = delete;
    LogDlg(LogDlg&&) = delete;
    LogDlg& operator=(LogDlg&&) = delete;

    /**
     * @returns The directory git runs in: the working tree root, or the bare repository.
     */
    [[nodiscard]] const QString& workingTree() const;

    /**
     * @returns The paths the log is limited to, relative to the working tree root; empty for the whole history.
     */
    [[nodiscard]] const QStringList& pathFilter() const;

    /**
     * @returns The commit list.
     */
    [[nodiscard]] GitLogList *logList() const;

    /**
     * @returns The selected commit's changed files.
     */
    [[nodiscard]] GitStatusListCtrl *changedFileList() const;

    /**
     * @returns The text of the message pane.
     */
    [[nodiscard]] QString messageText() const;

    /**
     * @returns The message pane.
     */
    [[nodiscard]] QTextBrowser *messageView() const;

    /**
     * Selects the next listed commit, searching down and wrapping round, whose hash starts with hash
     * (CLogDlg::JumpToGitHash). Nothing happens when no commit is selected or none matches.
     * @param hash The full or abbreviated hash, in lowercase.
     */
    void jumpToGitHash(const QString& hash);

    /**
     * @returns The Refresh button.
     */
    [[nodiscard]] QPushButton *refreshButton() const;

    /**
     * @returns The Walk Behavior button, checked while any of its entries is on.
     */
    [[nodiscard]] QPushButton *walkBehaviorButton() const;

    /**
     * @returns The View button.
     */
    [[nodiscard]] QPushButton *viewButton() const;

    /**
     * @returns The menu the Walk Behavior button opens.
     */
    [[nodiscard]] QMenu *walkBehaviorMenu() const;

    /**
     * @returns The menu the View button opens.
     */
    [[nodiscard]] QMenu *viewMenu() const;

    /**
     * @returns The menu the search box's icon opens.
     */
    [[nodiscard]] QMenu *filterMenu() const;

    /**
     * @returns The search box.
     */
    [[nodiscard]] QLineEdit *searchEdit() const;

    /**
     * @returns The From date picker.
     */
    [[nodiscard]] QDateEdit *dateFrom() const;

    /**
     * @returns The To date picker.
     */
    [[nodiscard]] QDateEdit *dateTo() const;

    /**
     * Makes the dialog pick one commit for another dialog (CLogDlg::SetSelect): OK needs exactly one commit selected,
     * and there is a Cancel.
     * @param select Whether to pick.
     */
    void setSelect(bool select);

    /**
     * Selects a revision once it is listed (CLogDlg::SetParams' hightlightRevision, which OnInitDialog stores as
     * m_lastSelectedHash). A revision that names no commit is ignored.
     * @param revision A ref or a hash.
     */
    void setHighlightRevision(const QString& revision);

    /**
     * @returns The commit picked with OK (CLogDlg::GetSelectedHash), or empty.
     */
    [[nodiscard]] const QString& selectedHash() const;

    /**
     * @returns The dialog's OK and Cancel buttons.
     */
    [[nodiscard]] QDialogButtonBox *buttonBox() const;

    /**
     * Records the commit picked (CLogDlg::OnOK) and closes the dialog.
     */
    void accept() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * What the View menu does to files outside the paths the log is limited to (CLogDlg::m_iHidePaths).
     */
    enum class HidePaths {
        None,
        Hide,
        Gray,
    };

    void loadSettings();
    void saveLabelSettings() const;
    void createWidgets();
    QLayout *createFilterBar();
    QLayout *createBottomRow();
    void createWalkBehaviorMenu();
    void createViewMenu();
    /// Attaches the Walk Behavior and View menus to their buttons.
    void attachMenus();
    void createFilterMenu();
    QAction *addCheckAction(QMenu *menu, const QString& text, bool checked);
    void addLabelAction(QMenu *menu, const QString& text, bool *shown);
    void addFilterAction(const QString& text, unsigned int bit);
    void refresh(bool clearFilter);
    void onSearchTextChanged(const QString& text);
    void onLogLoaded();
    void onFromDateChanged();
    void onToDateChanged();
    void onFilterIconClicked();
    void onFilterActionTriggered(unsigned int bit);
    void onCompressedGraph(int mode);
    void onHidePaths(HidePaths mode);
    void applyRefMask();
    void handleShowLabels();
    void updateWalkBehaviorChecked();
    void updateFilterActions();
    void setFilterCueText();
    void updateSearchValidity();
    void setWalkControlsEnabled(bool enabled);
    void enableOkButton();
    [[nodiscard]] QList<GitStatusListEntry> applyHidePaths(QList<GitStatusListEntry> entries) const;
    void onSelectedRevChanged();
    void fillLogMessage(const GitRevLoglist& rev);
    void onMsgViewLinkClicked(const QUrl& url);
    void onFilesLoaded(int row);
    void showFiles(const GitRevLoglist& rev);

    QString m_workingTree;
    QStringList m_pathFilter;
    GitLogList *m_logList = nullptr;
    QTextBrowser *m_msgView = nullptr;
    GitStatusListCtrl *m_changedFileList = nullptr;
    QDateEdit *m_dateFrom = nullptr;
    QDateEdit *m_dateTo = nullptr;
    QLineEdit *m_search = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_walkBehavior = nullptr;
    QPushButton *m_view = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    bool m_select = false; // CLogDlg::m_bSelect
    QString m_selectedHash; // CLogDlg::m_sSelectedHash
    QMenu *m_walkMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_filterMenu = nullptr;
    QAction *m_compressedAction = nullptr;
    QAction *m_labeledAction = nullptr;
    QAction *m_hidePathsAction = nullptr;
    QAction *m_grayPathsAction = nullptr;
    QAction *m_regexAction = nullptr;
    QAction *m_caseAction = nullptr;
    QTimer m_filterTimer; // LOGFILTER_TIMER
    QTimer m_dateTimer; // LOGFTIME_TIMER
    GitLogOptions m_walkOptions;
    int m_compressedGraph = 0; // CLogDlg::m_iCompressedGraph: 1 compressed, 2 labeled commits only
    HidePaths m_hidePaths = HidePaths::Gray; // "unrelated paths" start in gray, as in OnInitDialog
    QStringList m_matchPaths; // The paths the log is limited to, directories ending in '/'
    unsigned int m_selectedFilters = 0; // CLogDlg::m_SelectedFilters
    bool m_filterWithRegex = false; // UseRegexFilter
    bool m_filterCaseSensitively = false; // FilterCaseSensitively
    bool m_showTags = true; // LogDialog\ShowTags
    bool m_showLocalBranches = true; // LogDialog\ShowLocalBranches
    bool m_showRemoteBranches = true; // LogDialog\ShowRemoteBranches
    bool m_showOtherRefs = true; // LogDialog\ShowOtherRefs
};

} // namespace kgt
