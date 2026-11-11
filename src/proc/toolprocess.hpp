// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Runs git difftool and git mergetool. Not from TortoiseGit, which starts its own TortoiseMerge (CAppUtils::StartExtDiff,
 * CAppUtils::StartExtMerge) and so never meets a missing or terminal-only tool. The tool runs attached, with no
 * terminal, so a tool git can't run is reported instead of failing silently.
 */

#pragma once

#include "apputils.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

class QProcess;
class QWidget;

namespace kgt
{

/**
 * Which git command runs the tool.
 */
enum class ToolMode {
    Diff, // git difftool
    Merge, // git mergetool
};

/**
 * One run of git difftool or git mergetool, kept until it ends so that its failure can be reported.
 */
class ToolProcess : public QObject
{
    Q_OBJECT

public:
    /**
     * Creates the run without starting it.
     * @param mode Which git command it is.
     * @param workingDirectory Where git runs.
     * @param arguments The arguments after "git".
     * @param parent The owner.
     */
    ToolProcess(ToolMode mode, const QString& workingDirectory, const QStringList& arguments, QObject *parent = nullptr);

    /**
     * Starts git, with stdin at end-of-file and its output in a file, so that a prompt, or a tool that wants a
     * terminal, ends instead of waiting invisibly.
     * @returns Whether git started.
     */
    [[nodiscard]] bool start();

    /**
     * Checks the configured tool before a run, since git can't tell us in time.
     * @param mode Which git command will run it.
     * @param configuredTool The tool git will use (diff.tool, otherwise merge.tool); empty if neither is set.
     * @param isInstalled Says whether an executable is on the PATH.
     * @returns Why the tool can't run, or an empty string if it can.
     */
    [[nodiscard]] static QString configError(ToolMode mode, const QString& configuredTool, const AppUtils::IsInstalled& isInstalled);

    /**
     * @param tool A git tool name.
     * @returns Whether it is one of git's tools that runs in a terminal.
     */
    [[nodiscard]] static bool isTerminalTool(const QString& tool);

    /**
     * @param mode Which git command ran.
     * @param exitCode Its exit code.
     * @param output What it printed.
     * @returns Whether git couldn't run the tool. git mergetool also fails when the tool ran but the file is still
     *          unresolved, which is the user's choice and not reported.
     */
    [[nodiscard]] static bool toolFailed(ToolMode mode, int exitCode, const QString& output);

    /**
     * @param mode Which git command it is.
     * @param detail Why it failed.
     * @returns TortoiseGit's message for a tool that couldn't be started, with the detail.
     */
    [[nodiscard]] static QString failureMessage(ToolMode mode, const QString& detail);

    /**
     * Starts a run owned by the application, which reports a failure in a message box.
     * @param parent The parent of the message box; may be null, and may be gone by then.
     * @param mode Which git command it is.
     * @param workingDirectory Where git runs.
     * @param arguments The arguments after "git".
     * @returns Whether git started; if not, the user has been told.
     */
    static bool launch(QWidget *parent, ToolMode mode, const QString& workingDirectory, const QStringList& arguments);

    /**
     * Waits for every run launch() started, so that kortoisegitproc doesn't end them by exiting.
     */
    static void waitForAll();

Q_SIGNALS:
    /**
     * git couldn't run the tool.
     * @param message For the user.
     */
    void failed(const QString& message);

    /**
     * git has ended, after failed() if it failed.
     */
    void finished();

private:
    /**
     * Handles QProcess::finished.
     * @param exitCode git's exit code.
     * @param crashed Whether git crashed.
     */
    void onFinished(int exitCode, bool crashed);

    ToolMode m_mode;
    QTemporaryFile m_output; // What git and the tool print. A pipe would keep Vim waiting, but a file lets it exit
    QProcess *m_process;
};

} // namespace kgt
