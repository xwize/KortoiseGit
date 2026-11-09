// SPDX-FileCopyrightText: 2009 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/StashCommand.h. StashApplyCommand and StashPopCommand are not
 * ported yet.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Stashes the working tree's changes: the Stash dialog asks how, then git stash push runs in ProgressDlg.
 */
class StashSaveCommand : public Command
{
public:
    StashSaveCommand();

    /**
     * Stashes the changes of the working tree holding the path. --msg fills in the dialog's message.
     * @returns True if the changes were stashed and the progress dialog was closed with Close.
     * @throws RuntimeError if the path is not in a working tree.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
