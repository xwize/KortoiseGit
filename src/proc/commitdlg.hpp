// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CommitDlg.h (CCommitDlg): the commit dialog. This is its core, in
 * TortoiseGit's default mode without staging support: the checked files are what gets committed, and the index
 * is prepared for them when Commit is clicked.
 */

#pragma once

#include "utils/error.hpp"

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
    bool ignoreErrors = false; // A failure doesn't stop the commit (CMassiveGitTask's ignoreErrors)
    bool always = false; // Runs even after an earlier step failed: puts the index back as it was
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
     * @param parent Parent widget, or nullptr for a top-level dialog.
     * @throws RuntimeError (category Git) if the first path is not inside a working tree.
     */
    explicit CommitDlg(const QStringList& paths, QWidget *parent = nullptr);
    ~CommitDlg() override;

    CommitDlg(const CommitDlg&) = delete;
    CommitDlg& operator=(const CommitDlg&) = delete;
    CommitDlg(CommitDlg&&) = delete;
    CommitDlg& operator=(CommitDlg&&) = delete;

    /**
     * Builds the git commands for a commit of the listed files.
     * @param model The file list; its listed entries and their check states are used.
     * @returns The commands that prepare the index, and those that restore it after the commit.
     */
    [[nodiscard]] static CommitPlan planCommit(const GitStatusListModel& model);

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
    void updateOkButton();
    void onShowUnversionedToggled(bool checked);
    void runNextStep();
    void onStepFinished();
    void finishCommit();
    void setBusy(bool busy);

    QString m_workingTree;
    QLineEdit *m_commitTo = nullptr;
    QPlainTextEdit *m_message = nullptr;
    GitStatusListCtrl *m_fileList = nullptr;
    QLabel *m_statistics = nullptr;
    QCheckBox *m_showUnversioned = nullptr;
    QPushButton *m_okButton = nullptr;
    KMessageWidget *m_errorView = nullptr;
    QList<CommitStep> m_steps; // The commit's steps still to run
    std::unique_ptr<GitCall> m_stepCall; // The step running
    CommitStep m_runningStep;
    std::unique_ptr<QTemporaryFile> m_messageFile; // The message given to git commit -F
    std::optional<Error> m_commitError; // The first failure of the running commit
    bool m_isCommitting = false;
};

} // namespace kgt
