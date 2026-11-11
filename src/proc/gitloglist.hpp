// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitLogList.h (CGitLogList) and GitLogListBase.h (CGitLogListBase): the
 * commit list of the log dialog. TortoiseGit's owner-drawn CListCtrl becomes a QTreeView over a GitLogListModel
 * with a delegate that draws the ref labels and action icons; its LogThread becomes git log read as it runs.
 */

#pragma once

#include "gitloglistmodel.hpp"
#include "logdlgfilter.hpp"

#include "utils/error.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTreeView>

#include <memory>
#include <optional>
#include <utility>

class QMenu;

namespace kgt
{

class GitCall;

/**
 * The log's commit list. The owning dialog sets the working tree and the path filter and calls reload(); the
 * commits appear as git log writes them, so the UI never waits for the whole history.
 */
class GitLogList : public QTreeView
{
    Q_OBJECT

public:
    /**
     * Which commits a walk lists (CGitLogListBase::FilterShow), with TortoiseGit's FILTERSHOW_* values.
     */
    enum FilterShow : unsigned int { // NOLINT(cppcoreguidelines-use-enum-class): Combined as bit flags, as in TortoiseGit
        FilterShowRefs = 1, // FILTERSHOW_REFS: commits with a label, and HEAD
        FilterShowMergePoints = 2, // FILTERSHOW_MERGEPOINTS: merges and forks
        FilterShowAnyCommit = 4, // FILTERSHOW_ANYCOMMIT
        FilterShowAll = FilterShowAnyCommit | FilterShowRefs | FilterShowMergePoints, // FILTERSHOW_ALL
    };

    using TagInfo = QHash<QString, QString>; // The annotated tags' text by full ref name

    /**
     * What CopySelectionToClipBoard writes for each commit (TortoiseGit's ID_COPYCLIPBOARD*).
     */
    enum class CopyFormat {
        Full, // ID_COPYCLIPBOARDFULL
        FullNoPaths, // ID_COPYCLIPBOARDFULLNOPATHS
        Hash, // ID_COPYCLIPBOARDHASH
        AuthorsFull, // ID_COPYCLIPBOARDAUTHORSFULL
        AuthorsName, // ID_COPYCLIPBOARDAUTHORSNAME
        AuthorsEmail, // ID_COPYCLIPBOARDAUTHORSEMAIL
        Subjects, // ID_COPYCLIPBOARDSUBJECTS
        Messages, // ID_COPYCLIPBOARDMESSAGES
    };

    /**
     * @param parent The parent widget.
     */
    explicit GitLogList(QWidget *parent = nullptr);
    ~GitLogList() override;

    GitLogList(const GitLogList&) = delete;
    GitLogList& operator=(const GitLogList&) = delete;
    GitLogList(GitLogList&&) = delete;
    GitLogList& operator=(GitLogList&&) = delete;

    /**
     * @param root The directory git log runs in: the working tree root, or a bare repository. Takes effect on the
     *             next reload().
     */
    void setWorkingTree(const QString& root);

    /**
     * Limits the log to the commits that touched some paths, as TortoiseGit does for a file or subfolder. Takes
     * effect on the next reload().
     * @param paths Pathspecs relative to the working tree root, or empty for the whole history.
     */
    void setPathFilter(const QStringList& paths);

    /**
     * Lists a revision range instead of HEAD's history (the range argument of CGitLogListBase::FillGitLog). Takes
     * effect on the next reload().
     * @param range Such as "upstream..branch", or empty for HEAD.
     * @param noMerges Whether to leave merge commits out (CGit::LOG_INFO_NO_MERGE).
     */
    void setRange(const QString& range, bool noMerges = false);

    /**
     * Sets how history is walked (CGitLogListBase::m_ShowMask and m_Filter). Takes effect on the next reload().
     * @param options The walk.
     */
    void setWalkOptions(const GitLogOptions& options);

    /**
     * @returns How history is walked.
     */
    [[nodiscard]] const GitLogOptions& walkOptions() const;

    /**
     * Sets which commits are listed (CGitLogListBase::m_ShowFilter). Takes effect on the next reload().
     * @param showFilter FilterShow bits.
     */
    void setShowFilter(unsigned int showFilter);

    /**
     * Sets what the search box filters for (CGitLogListBase::m_LogFilter). Takes effect on the next reload().
     * @param filter The filter.
     */
    void setLogFilter(const LogDlgFilter& filter);

    /**
     * @returns What the search box filters for.
     */
    [[nodiscard]] const LogDlgFilter& logFilter() const;

    /**
     * Ported from CGitLogListBase::GetTimeRange: over every commit walked, listed or not.
     * @returns The oldest and the newest commit date.
     */
    [[nodiscard]] std::pair<QDateTime, QDateTime> timeRange() const;

    /**
     * Ported from CGitLogListBase::ShowText: abandons any load, empties the list and writes a text into it.
     * @param text Translated text.
     */
    void showText(const QString& text);

    /**
     * Offers the rebase actions (Pick, Squash, Edit, Skip) in the context menu, as the rebase dialog turns on
     * ID_REBASE_* in m_ContextMenuMask.
     * @param enabled Whether they are offered.
     */
    void setRebaseMenuEnabled(bool enabled);

    /**
     * @returns Whether the rebase actions are offered.
     */
    [[nodiscard]] bool isRebaseMenuEnabled() const;

    /**
     * Offers the log's own entries in the context menu (the rest of CGitLogListBase::OnContextMenu), as the log
     * dialog's m_ContextMenuMask does and the rebase dialog's doesn't.
     * @param enabled Whether they are offered.
     */
    void setLogMenuEnabled(bool enabled);

    /**
     * Ported from CGitLogListBase::IsMouseOnRefLabel.
     * @param row A row of the model.
     * @param pos A position in the viewport.
     * @returns The ref whose label is drawn there on that row, if any. A detached HEAD's label is no ref.
     */
    [[nodiscard]] std::optional<GitRef> refLabelAt(int row, const QPoint& pos) const;

    /**
     * Ported from CGitLogList::SetSelectedRebaseAction: gives the selected commits a rebase action, leaving out
     * those already done or being rebased, and refusing Squash for the last (oldest) commit and merge commits.
     * @param action RebasePick, RebaseSquash, RebaseEdit or RebaseSkip.
     */
    void setSelectedRebaseAction(unsigned int action);

    /**
     * Ported from CGitLogList::SetUnselectedRebaseAction: the same for the commits not selected.
     * @param action RebasePick, RebaseSquash, RebaseEdit or RebaseSkip.
     */
    void setUnselectedRebaseAction(unsigned int action);

    /**
     * Ported from CGitLogList::ShiftSelectedRebaseAction: steps the selected commits' actions on, Pick, Skip,
     * Edit, Squash and back to Pick, as the space bar does.
     */
    void shiftSelectedRebaseAction();

    /**
     * Empties the list and runs git log in the background, adding the commits page by page as git writes them
     * (CGitLogListBase::Refresh and LogThread). Any load still running is abandoned. The selected commit is selected
     * again when it is listed (m_lastSelectedHash).
     */
    void reload();

    /**
     * @returns Whether a reload() is still running, the background work for its filter included.
     */
    [[nodiscard]] bool isBusy() const;

    /**
     * @returns The model holding the commits, in git's order.
     */
    [[nodiscard]] GitLogListModel *logModel() const;

    /**
     * Asks for a commit's files in the background, ahead of earlier requests (CGitLogListBase's AsyncDiffThread
     * takes the newest request first). filesLoaded() follows; nothing happens if they are already known. The list
     * asks for the commits it draws by itself.
     * @param row A row of the model.
     */
    void requestFiles(int row);

    /**
     * @returns The row of the one selected commit, or -1 when none or several are selected.
     */
    [[nodiscard]] int selectedRow() const;

    /**
     * Ported from ID_COMPARE of CGitLogListBase::ContextMenuAction: starts a diff tool on a commit against the
     * working tree. When renames are followed it may first wait, in the background, for the files of the commits
     * above.
     * @param row The commit's row.
     */
    void compareWithWorkingTree(int row);

    /**
     * Ported from ID_COMPAREWITHPREVIOUS of CGitLogListBase::ContextMenuAction: starts a diff tool on one of a
     * commit's parents against the commit, waiting like compareWithWorkingTree().
     * @param row The commit's row.
     * @param parentNo Which parent, counting from 0.
     */
    void compareWithPrevious(int row, qsizetype parentNo);

    /**
     * Ported from the text CGitLogListBase::CopySelectionToClipBoard builds, with newlines for its CRLF.
     * @param format What to write for each commit.
     * @param revs The commits, in list order. Full needs their files loaded.
     * @param tags The annotated tags' text, for the two Full formats.
     * @returns The clipboard text.
     */
    [[nodiscard]] static QString copyText(CopyFormat format, const QList<GitRevLoglist>& revs, const TagInfo& tags);

    /**
     * Ported from ID_COPYCLIPBOARDBRANCHTAG in CGitLogListBase::ContextMenuAction.
     * @param refs The commit's full ref names, sorted.
     * @param label The ref whose label was right-clicked, if any.
     * @returns The label's short name, or else every full ref name, one per line.
     */
    [[nodiscard]] static QString refNamesText(const QStringList& refs, const std::optional<GitRef>& label);

Q_SIGNALS:
    /**
     * A reload() has finished: every commit is listed.
     */
    void loaded();

    /**
     * A reload() failed; the commits read before the failure stay listed and the empty list shows the message.
     * @param error What went wrong.
     */
    void loadFailed(const kgt::Error& error);

    /**
     * A commit's files have been fetched, or could not be: its filesState says which.
     * @param row The commit's row.
     */
    void filesLoaded(int row);

    /**
     * The selection has changed: a different commit, several or none are selected.
     */
    void selectedRevChanged();

    /**
     * The user changed rebase actions (CGitLogListBase::m_RebaseActionMessage).
     */
    void rebaseActionChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    /**
     * What CGitLogListBase::OnContextMenu works out about the commit the menu is for, when one is selected.
     */
    struct LogMenuTarget {
        QString hash; // m_CommitHash
        QStringList refs; // The commit's m_HashMap entry: full names, sorted, without a detached HEAD
        std::optional<GitRef> label; // The ref whose label was right-clicked (IsMouseOnRefLabelFromPopupMenu)
        bool isStash = false; // IsOnStash
        int row = -1; // The commit's row
        QStringList parents; // GetParentHashes
        bool isHead = false; // isHeadCommit
    };

    /**
     * A Compare entry following renames, waiting for the changed files of the commits it has to walk.
     */
    struct PendingCompare {
        int row = -1; // The commit's row
        QString rev1; // The right-hand revision; empty for the working tree
        QString rev2; // The left-hand revision
        QSet<int> waitingFor; // The rows whose files haven't arrived yet
    };

    /**
     * A copy waiting for the data TortoiseGit reads synchronously: the commits' files and the tag info.
     */
    struct PendingCopy {
        CopyFormat format = CopyFormat::Full; // What to write
        QList<int> rows; // The selected rows when it was asked for, top first
        TagInfo tagInfo; // Read for the two Full formats
    };

    void showRebaseMenu(QContextMenuEvent *event);
    void showLogMenu(QContextMenuEvent *event);
    [[nodiscard]] bool isOnStash(int row) const;
    void addSingleCommitEntries(QMenu& menu, const LogMenuTarget& target);
    void addSwitchBranchEntry(QMenu& menu, const LogMenuTarget& target);
    void addRevertEntry(QMenu& menu, const LogMenuTarget& target);
    [[nodiscard]] bool isMergeActive() const;
    void addCompareEntries(QMenu& menu, const LogMenuTarget& target);
    [[nodiscard]] QStringList parentHashes(const GitRevLoglist& rev);
    [[nodiscard]] QString parentInfo(qsizetype number, const QString& hash) const;
    void startCompare(int row, const QString& rev1, const QString& rev2);
    void finishCompare(const PendingCompare& compare);
    void onCompareFilesLoaded(int row);
    [[nodiscard]] bool addPushEntry(QMenu& menu, const LogMenuTarget& target);
    [[nodiscard]] bool addDeleteEntries(QMenu& menu, const LogMenuTarget& target);
    void addClipboardEntries(QMenu& menu, const std::optional<LogMenuTarget>& target);
    void copySelectionToClipboard(CopyFormat format);
    void onCopyTagInfoFinished();
    void finishCopyIfReady();
    void cancelCopy();
    void resetToCommit(const LogMenuTarget& target);
    void createTag(const LogMenuTarget& target);
    void createBranch(const LogMenuTarget& target);
    void switchToRev(const LogMenuTarget& target);
    void switchBranch(const QString& ref);
    void mergeRev(const LogMenuTarget& target);
    void rebaseToVersion(const LogMenuTarget& target);
    void revertRev(int parent);
    [[nodiscard]] bool revertSelectedCommits(int parent);
    void openCommitDlg();
    void push(const LogMenuTarget& target);
    void deleteRefs(const QStringList& refs);
    void reloadHashMap();
    [[nodiscard]] bool maySetRebaseAction(int row, unsigned int action) const;
    [[nodiscard]] QList<int> selectedRows() const;
    void startLog();
    void onOutputAvailable();
    void onLogFinished();
    void finishLoadIfDone();
    void handleRevs(const QList<GitRevLoglist>& revs);
    void showRevs(const QList<GitRevLoglist>& revs);
    [[nodiscard]] bool shouldShowRevInGraph(const GitRevLoglist& rev);
    [[nodiscard]] bool shouldShowRefs(const GitRevLoglist& rev) const;
    void restoreSelection(int firstRow);
    void onTagInfoFinished();
    void startNextSimpleLists();
    void onSimpleListFinished();
    void showQueuedRevs(qsizetype count);
    [[nodiscard]] bool takeRecords();
    void failLoad(const Error& error);
    void requestVisibleFiles();
    void startNextFiles();
    void startFilesCall();
    void onFilesFinished();
    void finishFiles(bool succeeded);

    GitLogListModel *m_model;
    QString m_workingTree;
    QStringList m_pathFilter;
    QString m_range; // The revisions listed, empty for HEAD
    GitLogOptions m_walkOptions; // CGitLogListBase::m_ShowMask and m_Filter
    unsigned int m_showFilter = FilterShowAll; // CGitLogListBase::m_ShowFilter
    LogDlgFilter m_logFilter; // CGitLogListBase::m_LogFilter
    bool m_rebaseMenu = false; // Whether the rebase actions are offered
    bool m_logMenu = false; // Whether the log's own entries are offered
    bool m_isLoading = false; // Whether a reload() is running
    std::unique_ptr<GitCall> m_logCall;
    std::unique_ptr<GitCall> m_tagInfoCall; // The for-each-ref that runs before git log when the filter needs it
    QHash<QString, QString> m_tagInfo; // The annotated tags' text by full ref name
    std::unique_ptr<GitCall> m_simpleListCall; // The diff-tree running for the start of m_simpleListQueue
    qsizetype m_simpleListPage = 0; // How many queued commits it is for
    QList<GitRevLoglist> m_simpleListQueue; // Walked commits waiting for their changed paths, in git's order
    QHash<QString, QStringList> m_simpleLists; // m_SimpleFileList by hash; a commit's paths never change
    QHash<QString, int> m_childCount; // commitChildren: how many walked commits have each hash as a parent
    QDateTime m_oldestCommit; // GetTimeRange's oldest
    QDateTime m_latestCommit; // GetTimeRange's latest
    QString m_lastSelectedHash; // m_lastSelectedHash: selected again when listed, empty once it has been
    QByteArray m_pending; // Output of git log that doesn't make a whole record yet
    QString m_errorMessage; // Shown in the empty list after a failed load
    QList<int> m_filesQueue; // TortoiseGit's m_AsynDiffList: rows waiting for their files, newest last
    std::unique_ptr<GitCall> m_filesCall; // The diff-tree running for m_filesRow
    int m_filesRow = -1; // The row whose files are being fetched, or -1
    QList<GitRevFiles> m_filesDone; // Its diffs fetched so far, one per parent
    std::optional<PendingCompare> m_pendingCompare; // The latest Compare entry still waiting for files
    std::optional<PendingCopy> m_copy; // The copy waiting for files or tag info, if any
    std::unique_ptr<GitCall> m_copyTagInfoCall; // The for-each-ref m_copy is waiting for
};

} // namespace kgt
