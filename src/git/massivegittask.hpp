// SPDX-FileCopyrightText: 2011-2016, 2019-2020, 2022-2024, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/MassiveGitTaskBase.h (CMassiveGitTaskBase): one git command applied to a list of
 * paths that may be too long for a command line.
 */

#pragma once

#include "git/git.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

namespace kgt
{

class GitCall;

/**
 * Runs a git command such as "add -f" on many paths at once. Like TortoiseGit, it hands git the paths with
 * --pathspec-from-file and --pathspec-file-nul rather than on the command line, so no list is too long; here the
 * list goes through stdin instead of a temporary file. The paths are taken literally (--literal-pathspecs), so a
 * file named "*.txt" adds only itself.
 *
 * Only commands that take --pathspec-from-file can be run this way: add, rm, reset, checkout, restore and stash.
 * TortoiseGit's fallback that splits the list over several command lines is not ported yet.
 */
class MassiveGitTask
{
public:
    /**
     * @param params The command and its options, such as {"add", "-f"}. If they hold "--end-of-options", the
     *               pathspec options go before it, so a revision may follow it.
     */
    explicit MassiveGitTask(const QStringList& params);

    /**
     * Sets the paths to run the command on (CMassiveGitTaskBase::SetPaths).
     * @param paths The paths, relative to the directory git runs in or absolute. Duplicates are dropped.
     */
    void setPaths(const QStringList& paths);

    /**
     * @returns The arguments after "git".
     */
    [[nodiscard]] QStringList arguments() const;

    /**
     * @returns The paths, NUL-separated, as git reads them from stdin.
     */
    [[nodiscard]] QByteArray pathSpecFile() const;

    /**
     * @returns Whether there is nothing to do: no paths were given.
     */
    [[nodiscard]] bool isEmpty() const;

    /**
     * Runs the command and waits for it (CMassiveGitTaskBase::Execute). Does nothing if there are no paths.
     * @param git Where to run it.
     * @throws RuntimeError (category Git) with git's error output if the command fails.
     */
    void run(const Git& git) const;

    /**
     * Starts the command without waiting for it.
     * @param git Where to run it.
     * @returns The running call; its output() throws if the command failed.
     */
    [[nodiscard]] std::unique_ptr<GitCall> runAsync(const Git& git) const;

private:
    QStringList m_params;
    QStringList m_paths;
};

} // namespace kgt
