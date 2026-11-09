// SPDX-FileCopyrightText: 2009 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/StashCommand.h.
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

/**
 * Restores the latest stash and keeps it.
 */
class StashApplyCommand : public Command
{
public:
    StashApplyCommand();

    /**
     * Applies the latest stash to the working tree holding the path.
     * @returns True if git applied it, even with conflicts.
     * @throws RuntimeError if the path is not in a working tree.
     */
    [[nodiscard]] bool execute() override;
};

/**
 * Restores the latest stash and removes it.
 */
class StashPopCommand : public Command
{
public:
    StashPopCommand();

    /**
     * Pops the latest stash into the working tree holding the path.
     * @returns True if git popped it, even with conflicts.
     * @throws RuntimeError if the path is not in a working tree.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
