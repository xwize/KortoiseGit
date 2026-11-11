// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CommitDlg.h (CCommitDlg): the commit dialog. This is its core, in
 * TortoiseGit's default mode without staging support: the checked files are what gets committed, and the index
 * is prepared for them when Commit is clicked. "Amend Last Commit" replaces HEAD instead of committing on top of it.
 */

#pragma once

#include "reghistory.hpp"
#include "utils/error.hpp"

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class KMessageWidget;
class QCheckBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QTemporaryFile;

namespace kgt
{

class GitCall;
class GitStatusListCtrl;
class GitStatusListModel;

/**
 * One git command of a commit, run after the one before it.
 */
struct CommitStep {
    QStringList arguments; // After "git"
    QByteArray input; // What git reads on stdin: the paths, which may not fit on a command line
    bool ignoreErrors = false; // A failure doesn't stop the commit (CMassiveGitTask's ignoreErrors)
    bool afterCommit = false; // Follows git commit and runs even if git commit failed, but not if preparing the index did
};

/**
 * The git commands that commit the checked files, ported from CCommitDlg::OnOK and
 * PrepareIndexForCommitWithoutStagingSupport (the git.exe branch).
 */
struct CommitPlan {
    QList<CommitStep> prepareIndex; // Stages the checked files, and takes the unchecked ones out of the index
    QList<CommitStep> restoreIndex; // Stages again the unchecked files that were added before (ReaddUnselectedAddedFilesAfterCommit)
    int checkedCount = 0; // How many files are committed
};

/**
 * The commit dialog: a message, the branch committed to, and the changed files with a checkbox each. Commit
 * stages the checked files, takes the unchecked ones out of the index, runs git commit with the message in a file,
 * and puts the unchecked files that were added back into the index. The dialog closes when the commit succeeds;
 * when git fails it shows the error and keeps the message.
 */
class CommitDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param paths The absolute paths the commit was asked for, all in one working tree.
     * @param logMessage The message to start with (TortoiseGit's /logmsg), or empty for commit.template, SQUASH_MSG and
     *   MERGE_MSG.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     * @throws RuntimeError (category Git) if the first path is not inside a working tree.
     */
    explicit CommitDlg(const QStringList& paths, const QString& logMessage = QString(), QWidget *parent = nullptr);
    ~CommitDlg() override;

    CommitDlg(const CommitDlg&) = delete;
    CommitDlg& operator=(const CommitDlg&) = delete;
    CommitDlg(CommitDlg&&) = delete;
    CommitDlg& operator=(CommitDlg&&) = delete;

    /**
     * Builds the git commands for a commit of the listed files.
     * @param model The file list; its listed entries and their check states are used.
     * @param amendAgainstParent Whether the list is against HEAD~1 because the last commit is amended: unchecked
     *        files are then reset to HEAD~1, so the last commit's changes to them are left out.
     * @returns The commands that prepare the index, and those that restore it after the commit.
     */
    [[nodiscard]] static CommitPlan planCommit(const GitStatusListModel& model, bool amendAgainstParent = false);

    /**
     * @returns The changed-files list.
     */
    [[nodiscard]] GitStatusListCtrl *fileList() const;

    /**
     * @returns The message editor.
     */
    [[nodiscard]] QPlainTextEdit *messageEdit() const;

    /**
     * @returns The Commit button.
     */
    [[nodiscard]] QPushButton *commitButton() const;

    /**
     * @returns The "Show Unversioned Files" checkbox.
     */
    [[nodiscard]] QCheckBox *showUnversionedButton() const;

    /**
     * @returns The "Amend Last Commit" checkbox. Clicking it swaps the message, as the user's click does.
     */
    [[nodiscard]] QCheckBox *amendButton() const;

    /**
     * @returns The "Add Signed-off-by" button.
     */
    [[nodiscard]] QPushButton *signOffButton() const;

    /**
     * Ported from CCommitDlg::InsertMenuItems: the message editor's context menu, with "Paste last commit message" and
     * "Paste recent message..." added when there are stored messages.
     * @param parent The menu's parent.
     * @returns The menu, owned by @p parent.
     */
    [[nodiscard]] QMenu *createMessageContextMenu(QWidget *parent);

    /**
     * Ported from CCommitDlg::ReloadHistoryEntries.
     * @returns Where the commit messages are kept, TortoiseGit's History\commit<UUID>.
     */
    [[nodiscard]] static KConfigGroup historyGroup();

    /**
     * Ported from CCommitDlg::GetSignedOffByLine.
     * @param workingTree The working tree, whose user name and email are used.
     * @returns "Signed-off-by: name <email>".
     */
    [[nodiscard]] static QString signedOffByLine(const QString& workingTree);

    /**
     * Ported from CCommitDlg::OnBnClickedSignOff: appends the Signed-off-by line, after a blank line unless the last
     * line is already a -by: trailer. Does nothing if the message has the line already.
     * @param message The message.
     * @param line The Signed-off-by line, from signedOffByLine().
     * @returns The message with the line.
     */
    [[nodiscard]] static QString addSignOff(const QString& message, const QString& line);

    /**
     * @returns Whether a commit is running.
     */
    [[nodiscard]] bool isCommitting() const;

    /**
     * Commits the checked files, as clicking Commit does (CCommitDlg::OnOK). Does nothing while Commit is
     * disabled.
     */
    void commit();

    /**
     * Closes the dialog unless a commit is running.
     */
    void reject() override;

Q_SIGNALS:
    /**
     * A commit failed; the dialog stays open, showing the error.
     * @param error What went wrong.
     */
    void commitFailed(const kgt::Error& error);

private:
    void createWidgets();
    /**
     * Puts the starting message in the editor, and shows any file that couldn't be loaded.
     * @param logMessage The message passed in, which wins if it isn't empty.
     */
    void loadInitialMessage(const QString& logMessage);
    void refresh();
    void onListLoaded();
    void onAmendClicked(bool checked);
    [[nodiscard]] bool isAmending() const;
    [[nodiscard]] bool isCherryPickActive() const;
    void updateOkButton();
    [[nodiscard]] bool confirmNoConflicts();
    void onShowUnversionedToggled(bool checked);
    void runNextStep();
    void onStepFinished();
    void finishCommit();
    void setBusy(bool busy);
    /**
     * Ported from CCommitDlg::ReloadHistoryEntries.
     */
    void reloadHistoryEntries();
    /**
     * Stores the message, and the one typed before amend was ticked, as OnOK and OnCancel do.
     * @param message The message to store, or an empty string for none.
     */
    void addToHistory(const QString& message);
    void onShowMessageContextMenu(const QPoint& pos);
    /**
     * Ported from CCommitDlg::OnBnClickedHistory.
     */
    void onHistory();
    /**
     * Ported from CCommitDlg::OnBnClickedSignOff.
     */
    void onSignOff();

    QString m_workingTree;
    QLineEdit *m_commitTo = nullptr;
    QPlainTextEdit *m_message = nullptr;
    GitStatusListCtrl *m_fileList = nullptr;
    QLabel *m_statistics = nullptr;
    QCheckBox *m_showUnversioned = nullptr;
    QCheckBox *m_amendButton = nullptr; // m_bCommitAmend
    QPushButton *m_signOffButton = nullptr;
    QPushButton *m_okButton = nullptr;
    KMessageWidget *m_errorView = nullptr;
    QList<CommitStep> m_steps; // The commit's steps still to run
    std::unique_ptr<GitCall> m_stepCall; // The step running
    CommitStep m_runningStep;
    std::unique_ptr<QTemporaryFile> m_messageFile; // The message given to git commit -F
    std::optional<Error> m_commitError; // The first failure of the running commit
    QString m_amendStr; // HEAD's message, as last shown while amending (m_AmendStr)
    QString m_noAmendStr; // The message typed before amend was ticked (m_NoAmendStr)
    QString m_logTemplate; // commit.template's text (m_sLogTemplate)
    RegHistory m_history; // The commit messages used before (m_History)
    bool m_amendDiffToLastCommit = false; // HEAD is a root commit, so amending compares with HEAD itself
    bool m_mergeActive = false; // A merge is being concluded (CTGitPath::IsMergeActive)
    bool m_isCommitting = false;
};

} // namespace kgt
