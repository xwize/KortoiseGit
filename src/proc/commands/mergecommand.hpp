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
     * @returns True if git merge succeeded.
     */
    static bool merge(const QString& workingTree, const QString& initialRefName = {}, QWidget *parent = nullptr);
};

} // namespace kgt
