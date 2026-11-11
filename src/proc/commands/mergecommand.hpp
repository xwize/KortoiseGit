// SPDX-FileCopyrightText: 2009-2013, 2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/MergeCommand.h.
 */

#pragma once

#include "commands/command.hpp"
#include "progressdlg.hpp"

#include <QStringList>

class QWidget;

namespace kgt
{

struct MergeOptions;

/**
 * Merges ("merge"): asks what in MergeDlg, then runs git merge in ProgressDlg, as CAppUtils::Merge and DoMerge do.
 * With --abort it aborts the merge in progress instead, through MergeAbortDlg (CAppUtils::MergeAbort).
 */
class MergeCommand : public Command
{
public:
    MergeCommand();

    /**
     * Shows MergeDlg and, if accepted, the merge's progress; or, with --abort, MergeAbortDlg.
     * @returns True if git merge (or the reset aborting it) succeeded.
     */
    [[nodiscard]] bool execute() override;

    /**
     * Ported from DoMerge's command line.
     * @param options What to merge and how; options.version must already be FixBranchName'd.
     * @param messageFile The file holding options.message, passed with -F; ignored if the message is empty.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList mergeArguments(const MergeOptions& options, const QString& messageFile);

    /**
     * Ported from CAppUtils::Merge and DoMerge, without the post-command buttons.
     * @param workingTree The working tree's root.
     * @param initialRefName What MergeDlg selects first (m_initialRefName); empty for its default.
     * @param parent Parent of the dialogs.
     * @param showStashPop Whether to offer Stash Pop after a successful merge, for a merge after stashing.
     * @returns True if git merge succeeded.
     */
    static bool merge(const QString& workingTree, const QString& initialRefName = {}, QWidget *parent = nullptr, bool showStashPop = false);

private:
    /**
     * Ported from DoMerge: runs git merge in ProgressDlg, which offers the follow-ups of its result.
     * @param workingTree The working tree's root.
     * @param options What to merge and how; options.version as chosen, not yet FixBranchName'd.
     * @param showStashPop Whether to offer Stash Pop after a successful merge.
     * @param parent Parent of the dialogs.
     * @returns True if git merge succeeded.
     */
    static bool doMerge(const QString& workingTree, const MergeOptions& options, bool showStashPop, QWidget *parent);

    /**
     * The success half of DoMerge's m_PostCmdCallback: Stash Pop, then Commit after --no-commit or --squash, or
     * else Remove branch and Push for a branch.
     * @param workingTree The working tree's root.
     * @param options What was merged.
     * @param showStashPop Whether to offer Stash Pop.
     * @param parent Parent of the dialogs.
     * @param[in,out] postCmdList Where to add the follow-ups.
     */
    static void addSuccessPostCmds(const QString& workingTree, const MergeOptions& options, bool showStashPop, QWidget *parent, PostCmdList& postCmdList);

    /**
     * DoMerge's Remove branch follow-up: asks, then deletes the merged branch with git branch -D.
     * @param workingTree The working tree's root.
     * @param branch The branch, as chosen in MergeDlg.
     * @param parent Parent of the messages.
     */
    static void removeBranch(const QString& workingTree, const QString& branch, QWidget *parent);
};

} // namespace kgt
