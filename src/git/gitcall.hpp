// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/Git.h (CGitCall): one run of the git executable, started straight away and
 * finished either through the event loop or by waiting for it.
 */

#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace kgt
{

/**
 * How a git command treats the repository's locks.
 */
enum class RunMode {
    ReadWrite, // An ordinary command that may take the index lock
    ReadOnly, // A read-only query such as status: GIT_OPTIONAL_LOCKS=0, so it never contends with the user's own git
};

/**
 * One git process. It is started by the constructor, never through a shell, with LC_ALL=C so its output doesn't
 * depend on the locale. Connect to finished() and then call output(), or call waitForFinished() first to block.
 */
class GitCall : public QObject
{
    Q_OBJECT

public:
    /**
     * Starts git.
     * @param workingDirectory The directory git runs in.
     * @param arguments The arguments after "git".
     * @param mode Whether git may take the repository's optional locks.
     * @param input What git reads on stdin, such as a --pathspec-from-file=- list; stdin is empty if this is.
     */
    GitCall(const QString& workingDirectory, const QStringList& arguments, RunMode mode, const QByteArray& input = {});
    ~GitCall() override;

    GitCall(const GitCall&) = delete;
    GitCall& operator=(const GitCall&) = delete;
    GitCall(GitCall&&) = delete;
    GitCall& operator=(GitCall&&) = delete;

    /**
     * Blocks until git exits, without an event loop: safe on a worker thread such as Dolphin's retrieval thread.
     */
    void waitForFinished();

    /**
     * @returns Whether git has exited or failed to start, so output() may be called.
     */
    [[nodiscard]] bool isFinished() const;

    /**
     * Takes what git has written to stdout so far, for a caller that parses long output as it arrives.
     * @returns The output since the last call; output() then holds only what was not taken.
     */
    [[nodiscard]] QByteArray takeAvailableOutput();

    /**
     * @returns Everything git wrote to stdout that takeAvailableOutput() has not taken. Must only be called once
     *          isFinished().
     * @throws RuntimeError (category Git) if git failed to start, crashed or exited non-zero. Its message holds the
     *         command line and git's stderr.
     */
    [[nodiscard]] QByteArray output() const;

    /**
     * @returns The command line, for messages: "git" followed by the arguments, quoted where they hold spaces.
     */
    [[nodiscard]] QString commandLine() const;

Q_SIGNALS:
    /**
     * Git has written more to stdout: takeAvailableOutput() returns it.
     */
    void outputAvailable();

    /**
     * Git has exited or failed to start. Emitted once: from the event loop, or from inside waitForFinished().
     */
    void finished();

private:
    void onProcessFinished();

    QProcess m_process;
    QByteArray m_stdout;
    QByteArray m_stderr;
    bool m_isFinished = false;
};

} // namespace kgt
