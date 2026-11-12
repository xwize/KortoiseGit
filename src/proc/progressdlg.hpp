// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ProgressDlg.h (CProgressDlg): runs git commands one after another and
 * shows their output as it arrives.
 */

#pragma once

#include "gitclioutputparser.hpp"

#include <QDialog>
#include <QElapsedTimer>
#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <utility>

class QLabel;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QToolButton;

namespace kgt
{

/**
 * A follow-up the dialog offers when its commands end, TortoiseGit's PostCmd: Resolve, Commit, Push and the like.
 */
struct PostCmd {
    /**
     * @param icon The entry's icon.
     * @param label Translated, with an & for its mnemonic.
     * @param action What choosing it does.
     */
    PostCmd(QIcon icon, QString label, std::function<void()> action)
        : icon(std::move(icon))
        , label(std::move(label))
        , action(std::move(action))
    {
    }

    QIcon icon; // The entry's icon
    QString label; // Translated, with an & for its mnemonic
    std::function<void()> action; // Runs after the dialog has hidden, before it closes
};

using PostCmdList = QList<PostCmd>; // TortoiseGit's PostCmdList

/**
 * The "Git Command Progress" dialog. It starts its commands when first shown and streams git's merged stdout and
 * stderr into a log, redrawing progress lines in place as a console does. When they end it reports "Success" or
 * the exit code. Abort kills the running git together with everything it started (ssh, hooks) and closes the
 * dialog; Close is enabled once the commands have ended. Reusable for push, pull, fetch and the like.
 */
class ProgressDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param workingDirectory The directory git runs in.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit ProgressDlg(const QString& workingDirectory, QWidget *parent = nullptr);
    ~ProgressDlg() override;

    ProgressDlg(const ProgressDlg&) = delete;
    ProgressDlg& operator=(const ProgressDlg&) = delete;
    ProgressDlg(ProgressDlg&&) = delete;
    ProgressDlg& operator=(ProgressDlg&&) = delete;

    /**
     * Adds a command to run, TortoiseGit's m_GitCmdList. Must be called before the dialog is shown.
     * @param arguments The arguments after "git".
     * @param input What git reads on stdin, such as the paths of --pathspec-from-file=-. It gets no stdin if empty.
     */
    void addGitCmd(const QStringList& arguments, const QByteArray& input = {});

    /**
     * @param show Whether each command line goes into the log before its output, TortoiseGit's m_bShowCommand
     *             (on by default). Must be set before the commands start.
     */
    void setShowCommand(bool show);

    /**
     * @param text Shown in the log before anything runs, such as output already produced, TortoiseGit's m_PreText.
     *             Must be set before the commands start.
     */
    void setPreText(const QString& text);

    /**
     * Called when the commands end, before the result is shown, with the OR-ed exit status and an empty message.
     * It may change the status, such as to report a success that left conflicts as a failure, and may set a
     * message to append to the log. TortoiseGit's m_PostExecCallback.
     */
    using PostExecCallback = std::function<void(int& exitCode, QString& extraMsg)>;

    /**
     * @param callback Called when the commands end (see PostExecCallback). Must be set before they start.
     */
    void setPostExecCallback(PostExecCallback callback);

    /**
     * Called when the commands end, after the result is shown, with the OR-ed exit status and an empty list to fill
     * with the follow-ups to offer. TortoiseGit's m_PostCmdCallback.
     */
    using PostCmdCallback = std::function<void(int status, PostCmdList& postCmdList)>;

    /**
     * @param callback Called when the commands end (see PostCmdCallback). Must be set before they start.
     */
    void setPostCmdCallback(PostCmdCallback callback);

    /**
     * @returns The button offering the follow-ups, TortoiseGit's m_ctrlPostCmd: the first is its label and every one
     *          is in its menu. Hidden while there are none.
     */
    [[nodiscard]] QToolButton *postCmdButton() const;

    /**
     * Ported from CProgressDlg::OnBnClickedButton1: hides the dialog, runs a follow-up and closes the dialog with
     * Close's result.
     * @param index The follow-up's position in the list the callback filled.
     */
    void runPostCmd(qsizetype index);

    /**
     * Starts the commands, if they haven't been started. Showing the dialog does this; tests may call it directly.
     */
    void start();

    /**
     * @returns Whether every command has ended (or one failed to start).
     */
    [[nodiscard]] bool isDone() const;

    /**
     * @returns Whether a command is running.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @returns The exit codes of the commands OR-ed together, as TortoiseGit's m_GitStatus: 0 for success, and -1
     *          before the commands have ended or if one could not be started.
     */
    [[nodiscard]] int gitStatus() const;

    /**
     * @returns The text of the log window.
     */
    [[nodiscard]] QString logText() const;

    /**
     * @returns The status line above the progress bar: what git is doing, then "Success" or the failure.
     */
    [[nodiscard]] QString currentWork() const;

    /**
     * Abort's action, TortoiseGit's OnCancel: kills the running command, if any, and closes the dialog.
     */
    void reject() override;

Q_SIGNALS:
    /**
     * Every command has ended, or one failed to start; gitStatus() holds the result.
     */
    void commandsFinished();

protected:
    /**
     * Starts the commands the first time the dialog is shown, as TortoiseGit does in OnInitDialog.
     * @param event The show event.
     */
    void showEvent(QShowEvent *event) override;

private:
    /**
     * One git command to run.
     */
    struct GitCmd {
        QStringList arguments; // After "git"
        QByteArray input; // On stdin
    };

    /**
     * Starts the next command, or finishes if there is none.
     */
    void runNext();

    /**
     * Collects the running command's output.
     */
    void onReadyRead();

    /**
     * Moves on after a command ended: to the next one, or to the end on failure.
     */
    void onProcessFinished();

    /**
     * Shows what the parser has produced since the last call, TortoiseGit's UpdateCmdOutput.
     */
    void updateCmdOutput();

    /**
     * Updates the status line and progress bar from a progress line such as "Writing objects:  50% (1/2)".
     * @param text The newest output.
     */
    void updateProgressFromLine(const QString& text);

    /**
     * Shows the result, TortoiseGit's MSG_PROGRESSDLG_END handling.
     * @param status The OR-ed exit codes, or -1.
     */
    void finish(int status);

    /**
     * Appends bold coloured text to the log.
     * @param text The text.
     * @param failure Whether to colour it as a failure (red) rather than a success (blue).
     */
    void insertColorText(const QString& text, bool failure);

    /**
     * Kills the running command and everything it started, waiting for it to end.
     */
    void killProcess();

    /**
     * Asks the callback for the follow-ups and shows them in the menu button.
     */
    void showPostCmds();

    QLabel *m_currentWork;
    QProgressBar *m_progress;
    QPlainTextEdit *m_log;
    QPushButton *m_closeButton;
    QPushButton *m_abortButton;
    QToolButton *m_postCmdButton;
    QString m_workingDirectory;
    QList<GitCmd> m_gitCmdList;
    PostExecCallback m_postExecCallback;
    PostCmdCallback m_postCmdCallback;
    PostCmdList m_postCmdList;
    qsizetype m_nextCmd = 0; // Index in m_gitCmdList of the command to run next
    std::unique_ptr<QProcess> m_process; // The running command
    GitCliOutputParser m_parser;
    QTimer m_refreshTimer; // Shows the collected output every 125 ms, as TortoiseGit's REFRESHTIMER
    QElapsedTimer m_elapsed;
    unsigned int m_combinedStatus = 0; // The exit codes so far, OR-ed as TortoiseGit does
    int m_gitStatus = -1;
    QString m_preText;
    bool m_showCommand = true;
    bool m_started = false;
    bool m_done = false;
    bool m_abort = false;
};

} // namespace kgt
