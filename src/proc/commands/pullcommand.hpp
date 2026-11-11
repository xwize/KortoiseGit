// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/PullCommand.h.
 */

#pragma once

#include "commands/command.hpp"

#include <QStringList>

namespace kgt
{

struct PullFetchOptions;

/**
 * Pulls ("pull"): asks where from in PullFetchDlg, then runs git pull in ProgressDlg, as CAppUtils::Pull and
 * DoPull do; with "Launch Rebase After Fetch" it fetches and opens RebaseDlg instead. Several paths are pulled one after another, each of which must be a
 * working tree's root.
 */
class PullCommand : public Command
{
public:
    PullCommand();

    /**
     * Shows PullFetchDlg in pull mode and, if accepted, the pull's progress.
     * @returns True if every git pull succeeded.
     */
    [[nodiscard]] bool execute() override;

    /**
     * Ported from DoPull in CAppUtils.
     * @param options Where to pull from and how.
     * @returns The arguments after "git" for the pull.
     */
    [[nodiscard]] static QStringList pullArguments(const PullFetchOptions& options);

private:
    /**
     * Ported from CAppUtils::Pull and DoPull, without the post-command buttons.
     * @param workingTree The working tree's root.
     * @returns True if git pull succeeded.
     */
    [[nodiscard]] static bool pull(const QString& workingTree);
};

} // namespace kgt
