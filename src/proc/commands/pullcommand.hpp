// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/PullCommand.h.
 */

#pragma once

#include "commands/command.hpp"
#include "progressdlg.hpp"

#include <QStringList>

class QWidget;

namespace kgt
{

class Git;
struct PullFetchOptions;

/**
 * What a pull offers besides what follows from its result: CAppUtils::Pull's showPush and showStashPop.
 */
struct PullFollowUps {
    bool showPush = false; // showPush: offer Push after a successful pull
    bool showStashPop = false; // showStashPop: offer Stash Pop after a successful pull
};

/**
 * Pulls ("pull"): asks where from in PullFetchDlg, then runs git pull in ProgressDlg, as CAppUtils::Pull and
 * DoPull do; with "Launch Rebase After Fetch" it fetches and opens RebaseDlg instead. Several paths are pulled
 * one after another, each of which must be a working tree's root.
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

    /**
     * Ported from CAppUtils::Pull: asks where from in PullFetchDlg, then pulls (DoPull) or fetches and rebases.
     * @param workingTree The working tree's root.
     * @param followUps What to offer after a successful pull besides the pulled diff and log.
     * @param parent Parent of the dialogs.
     * @returns True if git pull succeeded.
     */
    static bool pull(const QString& workingTree, const PullFollowUps& followUps = {}, QWidget *parent = nullptr);

private:
    /**
     * Ported from DoPull: runs git pull in ProgressDlg, which offers the follow-ups of its result.
     * @param workingTree The working tree's root.
     * @param options Where to pull from and how.
     * @param followUps What to offer after a successful pull.
     * @param parent Parent of the dialogs.
     * @returns True if git pull succeeded.
     */
    static bool doPull(const QString& workingTree, const PullFetchOptions& options, const PullFollowUps& followUps, QWidget *parent);

    /**
     * The failure half of DoPull's m_PostCmdCallback: Resolve and Commit after conflicts; otherwise Merge unrelated
     * history if nothing is shared with the remote branch, Pull, Stash changes and Reset.
     * @param git Runs git in the working tree's root.
     * @param options What the pull was.
     * @param followUps What it offered on success, kept for a retry.
     * @param progress The progress dialog, parent of the messages.
     * @param[in,out] postCmdList Where to add the follow-ups.
     */
    static void
    addFailurePostCmds(const Git& git, const PullFetchOptions& options, const PullFollowUps& followUps, QWidget *progress, PostCmdList& postCmdList);
};

} // namespace kgt
