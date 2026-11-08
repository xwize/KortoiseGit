// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/RebaseDlg.h (CRebaseDlg): the Rebase dialog. Like TortoiseGit it does
 * not run git rebase -i: it checks out the upstream and cherry-picks the commits itself, one by one, following
 * each commit's action (Pick, Squash, Edit, Skip), and keeps its own state so it can stop, continue and abort.
 *
 * The work runs on a worker thread, as TortoiseGit's RebaseThread does. The worker runs git and keeps the rebase
 * state; everything that touches a widget or the commit list's model runs on the UI thread, the worker waiting
 * for it as TortoiseGit's SendMessage does. While the worker runs, the user cannot change the commits' actions.
 */

#pragma once

#include "git/git.hpp"
#include "git/gitrevloglist.hpp"

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QTabWidget;
class QThread;
class QToolButton;

namespace kgt
{

class GitLogList;
class GitStatusListCtrl;

/**
 * What RebaseCommand hands the dialog (CRebaseDlg's public members).
 */
struct RebaseOptions {
    QString upstream; // m_Upstream: the upstream to preselect; empty for the current branch's tracked branch
    bool force = false; // m_bForce: "Force Rebase" starts checked
};

/**
 * Ported from CRebaseDlg::SquashFirstMetaData: the author and date the squashed commit keeps, those of the first
 * commit of the squash.
 */
struct SquashFirstMetaData {
    QString name; // The author's name
    QString email; // The author's email
    QDateTime time; // The author date
    bool set = false; // Whether a commit has been recorded

    /**
     * @returns "name <email>", or an empty string if nothing is set.
     */
    [[nodiscard]] QString author() const;

    /**
     * @param now Whether the commit gets the current date rather than the recorded one (SquashDate 2).
     * @returns The --date and --author arguments of the squashed commit, or none if nothing is set.
     */
    [[nodiscard]] QStringList asParams(bool now) const;
};

/**
 * The Rebase dialog: Branch and Upstream to choose, the commits between them with their rebase actions, and below
 * them the Conflict Files, Commit Message and Log tabs.
 */
class RebaseDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * The dialog's stages, CRebaseDlg::RebaseStage.
     */
    enum class RebaseStage {
        ChooseBranch, // Choose_Branch: choosing branch, upstream and the actions
        ChooseCommitPickMode, // Choose_Commit_Pick_Mode: cherry-pick mode, not ported
        Start, // Start: about to check out the upstream
        Continue, // Continue: picking the next commit
        Abort, // Abort
        Finish, // Finish: every commit is picked; the branch is moved next
        Conflict, // Conclict: a pick stopped on a conflict
        Edit, // Edit: stopped to amend an Edit commit
        SquashEdit, // Squash_Edit: stopped to edit the message of a squash
        SquashConflict, // Squash_Conclict: a squash stopped on a conflict
        Done, // Done: finished
        Error, // Error: an error the rebase can't go on after
    };

    /**
     * The lower tabs (REBASE_TAB_*).
     */
    enum Tab { // NOLINT(cppcoreguidelines-use-enum-class): Used as QTabWidget indexes
        ConflictTab, // REBASE_TAB_CONFLICT: "Revision Files", then "Conflict Files" once started
        MessageTab, // REBASE_TAB_MESSAGE: "Commit Message"
        LogTab, // REBASE_TAB_LOG: "Log", added once started
    };

    /**
     * Opens the dialog and starts loading the branches.
     * @param workingTree The working tree's root.
     * @param options What RebaseCommand asked for.
     * @param parent Parent widget.
     */
    explicit RebaseDlg(const QString& workingTree, const RebaseOptions& options = {}, QWidget *parent = nullptr);
    ~RebaseDlg() override;

    RebaseDlg(const RebaseDlg&) = delete;
    RebaseDlg& operator=(const RebaseDlg&) = delete;
    RebaseDlg(RebaseDlg&&) = delete;
    RebaseDlg& operator=(RebaseDlg&&) = delete;

    /**
     * @returns The current stage.
     */
    [[nodiscard]] RebaseStage stage() const;

    /**
     * @returns Whether the worker thread is running (m_bThreadRunning).
     */
    [[nodiscard]] bool isThreadRunning() const;

    /**
     * @returns Whether the commit list is loading.
     */
    [[nodiscard]] bool isLoading() const;

    /**
     * @returns The Branch combo box.
     */
    [[nodiscard]] QComboBox *branchCombo() const;

    /**
     * @returns The Upstream combo box.
     */
    [[nodiscard]] QComboBox *upstreamCombo() const;

    /**
     * @returns The button that swaps Branch and Upstream.
     */
    [[nodiscard]] QToolButton *reverseButton() const;

    /**
     * @returns The "Force Rebase" checkbox.
     */
    [[nodiscard]] QCheckBox *forceCheck() const;

    /**
     * @returns The "Preserve merges" checkbox.
     */
    [[nodiscard]] QCheckBox *preserveMergesCheck() const;

    /**
     * @returns The commit list.
     */
    [[nodiscard]] GitLogList *commitList() const;

    /**
     * @returns The file list of the first tab: the selected commit's files, then the conflicted files.
     */
    [[nodiscard]] GitStatusListCtrl *fileList() const;

    /**
     * @returns The Commit Message editor.
     */
    [[nodiscard]] QPlainTextEdit *messageEdit() const;

    /**
     * @returns The Log output.
     */
    [[nodiscard]] QPlainTextEdit *logOutput() const;

    /**
     * @returns The lower tabs.
     */
    [[nodiscard]] QTabWidget *tabs() const;

    /**
     * @returns The Start Rebase / Continue / Commit / Amend / Done button.
     */
    [[nodiscard]] QPushButton *continueButton() const;

    /**
     * @returns The Abort button.
     */
    [[nodiscard]] QPushButton *abortButton() const;

    /**
     * @returns The status text below the progress bar.
     */
    [[nodiscard]] QLabel *statusLabel() const;

    /**
     * Ported from CRebaseDlg::OnBnClickedButtonUp: moves the selected commits up by one, or to the top.
     * @param toTop Whether to move them to the top (Shift).
     */
    void moveSelectedUp(bool toTop = false);

    /**
     * Ported from CRebaseDlg::OnBnClickedButtonDown: moves the selected commits down by one, or to the bottom.
     * @param toBottom Whether to move them to the bottom (Shift).
     */
    void moveSelectedDown(bool toBottom = false);

    /**
     * Ported from CRebaseDlg::OnBnClickedContinue: starts the rebase, or goes on with it from the stage it
     * stopped at; once Done, closes the dialog.
     */
    void onContinue();

    /**
     * Ported from CRebaseDlg::OnBnClickedAbort: asks, then restores the branch and HEAD as they were and closes
     * the dialog. While the worker runs, asks it to stop instead.
     */
    void onAbort();

    /**
     * Closing the dialog aborts, as CRebaseDlg::OnCancel does.
     */
    void reject() override;

Q_SIGNALS:
    /**
     * The commit list has been filled for the chosen branch and upstream (FetchLogList), or shows why not.
     */
    void logListFetched();

    /**
     * The worker thread has ended and the dialog shows the stage it stopped at.
     */
    void threadFinished();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * What a press of Continue leads to once its stage is dealt with.
     */
    enum class ContinueResult {
        Stop, // Stay at the stage: the user has to act, or it failed
        RunRebase, // Go on with RebaseThread
        Close, // The rebase is finished: close the dialog (OnOK)
    };

    /**
     * What to do about a commit that would be empty (IDS_CHERRYPICK_EMPTY's answer).
     */
    struct EmptyCommit {
        bool allowEmpty = false; // Commit it anyway, with --allow-empty
        bool skip = false; // Leave it out
    };

    /**
     * A git run whose failure is handled where it happens, as CGit::Run's return code is.
     */
    struct GitResult {
        bool ok = false; // Git exited with 0
        QString output; // Its stdout, or the error with git's stderr if it failed
    };

    // rebasedlg.cpp: the UI
    void createWidgets();
    void loadBranchInfo();
    void onBranchesLoaded();
    void fetchLogList();
    [[nodiscard]] bool checkFetchLogListRefs(QString& branchHash, QString& upstreamHash);
    void onLogListLoaded();
    void preserveMergesDropCommits();
    [[nodiscard]] bool markCherryPickedAsSkip();
    void addBranchToolTips(QComboBox *combo);
    void setAllRebaseAction(unsigned int action);
    void onSplitAllOptions(int entry);
    void onReverse();
    void onBrowse();
    void onOnto();
    void onOptionToggled();
    void onCommitSelectionChanged();
    void refreshFileList();
    void onRebaseActionChanged();
    void refresh();
    [[nodiscard]] bool handleListKey(int key, Qt::KeyboardModifiers modifiers);
    void setContinueButtonText();
    void setControlEnable();
    void updateProgress();
    void updateCurrentStatus();
    void setStatusText(const QString& text, bool warning);
    void onRebaseUpdateUi();
    void showStageMessage();
    void listConflictFile();
    void selectFirstConflict();
    void writeRebaseActiveFolder();
    void cleanUpRebaseActiveFolder();
    void checkRestoreStash();
    void finishDialog();
    void startThread(std::function<void()> job);
    void onThreadFinished();

    // rebasedlg_rebase.cpp: the rebase itself, run on the worker thread
    void continueJob();
    [[nodiscard]] ContinueResult continueRebase();
    [[nodiscard]] bool checkRebaseCondition();
    [[nodiscard]] bool fastForward();
    [[nodiscard]] bool continueSquashConflict();
    [[nodiscard]] bool continueConflict();
    [[nodiscard]] bool commitResolvedConflict(const GitRevLoglist& rev, const QString& message);
    [[nodiscard]] std::optional<EmptyCommit> checkResultingCommitEmpty(bool amend);
    [[nodiscard]] bool continueNotClean();
    [[nodiscard]] bool continueEdit();
    void rebaseThread();
    [[nodiscard]] bool startRebase();
    [[nodiscard]] int doRebase();
    [[nodiscard]] bool prepareSquash(const GitRevLoglist& rev, unsigned int mode);
    [[nodiscard]] std::optional<QStringList> pickOptions(const QString& hash, unsigned int mode, bool noCommit);
    [[nodiscard]] int runPick(int row, const GitRevLoglist& rev, unsigned int mode, const QStringList& options);
    void addToSquashMessage(const GitRevLoglist& rev);
    [[nodiscard]] int preserveMergesCommand(const GitRevLoglist& rev, unsigned int mode, const QString& noCommit, QStringList& command);
    [[nodiscard]] QStringList rewrittenParents(const GitRevLoglist& rev, const QString& currentHead, bool& parentRewritten);
    [[nodiscard]] std::optional<int> handlePickFailure(int row, const QString& hash, unsigned int mode, const QString& output, bool& retry);
    [[nodiscard]] int handlePickSuccess(int row, const QString& hash, unsigned int mode, bool noCommit);
    [[nodiscard]] bool finishRebase();
    [[nodiscard]] bool moveBranchToRebasedCommits();
    void rewriteNotes();
    [[nodiscard]] bool verifyNoConflict();
    void resetParentForSquash(const QString& commitMessage);
    [[nodiscard]] bool isNextCommitSquash();
    void goNext();
    [[nodiscard]] bool isEnd() const;
    [[nodiscard]] int currentCommitId();
    [[nodiscard]] int isCommitEmpty(const QString& hash);
    [[nodiscard]] bool runGitCmdRetryOrAbort(const QStringList& arguments);
    void abortJob();
    void restoreAfterAbort();
    [[nodiscard]] GitResult gitRun(const QStringList& arguments, const QByteArray& input = {}) const;
    [[nodiscard]] QString hashOf(const QString& ref) const;
    [[nodiscard]] std::optional<GitRevLoglist> getCommit(const QString& ref) const;
    [[nodiscard]] bool isFastForward(const QString& fromRef, const QString& toRef, QString *commonAncestor = nullptr) const;
    [[nodiscard]] bool checkCleanWorkTree(bool stagedOk = false) const;
    [[nodiscard]] bool isLocalBranch(const QString& name) const;
    [[nodiscard]] GitRevLoglist currentRev();
    [[nodiscard]] GitRevLoglist revAt(int row);
    [[nodiscard]] int rowCount();
    void setRebaseAction(int row, unsigned int action);
    void addRebaseAction(int row, unsigned int flags);
    void addLogString(const QString& text);
    void setActiveTab(int tab);
    [[nodiscard]] int askUser(int icon, const QString& text, const QStringList& buttons, const QString& checkText = {}, bool *checked = nullptr);
    void showError(const QString& text);
    void runOnUi(const std::function<void()>& job);

    Git m_git;
    RebaseOptions m_options;

    // Widgets
    QComboBox *m_branchCtrl;
    QToolButton *m_reverseButton;
    QComboBox *m_upstreamCtrl;
    QPushButton *m_browseButton;
    QPushButton *m_ontoButton;
    QSplitter *m_splitter;
    GitLogList *m_commitList;
    QToolButton *m_splitAllOptions;
    QPushButton *m_upButton;
    QPushButton *m_downButton;
    QCheckBox *m_preserveMergesCheck;
    QCheckBox *m_forceCheck;
    QTabWidget *m_tabs;
    GitStatusListCtrl *m_fileListCtrl;
    QPlainTextEdit *m_logMessageCtrl;
    QPlainTextEdit *m_outputRebase;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QPushButton *m_continueButton;
    QPushButton *m_abortButton;
    int m_splitAllEntry = 0; // The entry the Squash ALL button runs (CMenuButton's current entry)

    // Loading
    std::unique_ptr<GitCall> m_branchCall;
    std::unique_ptr<GitCall> m_tagCall;
    bool m_branchesLoaded = false;

    // The worker thread
    QThread *m_thread = nullptr;
    std::atomic<bool> m_threadRunning = false; // m_bThreadRunning
    std::atomic<bool> m_abortRequested = false; // m_bAbort
    std::atomic<RebaseStage> m_rebaseStage = RebaseStage::ChooseBranch; // m_RebaseStage
    bool m_closeAfterThread = false; // The job ended the dialog's work (OnOK or the end of an abort)
    bool m_acceptAfterThread = false; // ... with OnOK rather than OnCancel

    // What the rebase works on, taken from the controls when Continue is pressed (they are disabled from then on)
    QString m_branch; // m_BranchCtrl.GetString()
    QString m_upstream; // m_UpstreamCtrl.GetString()
    QString m_onto; // m_Onto: rebase onto this instead of the upstream
    bool m_force = false; // m_bForce
    bool m_preserveMerges = false; // m_bPreserveMerges
    QString m_messageText; // m_LogMessageCtrl.GetText() when Continue was pressed

    // The rebase state, CRebaseDlg's members of the same names
    bool m_isFastForward = false;
    QString m_origBranchHash;
    QString m_origUpstreamHash;
    QString m_origHeadBranch;
    QString m_origHeadHash;
    QString m_squashMessage;
    bool m_currentCommitEmpty = false;
    SquashFirstMetaData m_squashFirstMetaData;
    int m_currentRebaseIndex = -1;
    bool m_finishedRebase = false;
    bool m_stashed = false;
    bool m_autoSkipFailedCommit = false;
    QString m_statusText; // m_sStatusText
    QHash<QString, QString> m_rewrittenCommitsMap; // Old commit to new commit, for git notes copy
    QStringList m_forRewrite;
    QHash<QString, QStringList> m_droppedCommitsMap;
    QStringList m_currentCommits;
};

} // namespace kgt
