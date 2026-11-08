// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2019, 2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/DiffCommand.h.
 */

#pragma once

#include "commands/command.hpp"
#include "gitdiff.hpp"

namespace kgt
{

/**
 * Compares a file with HEAD, two revisions of it, or two files, in git difftool. A folder needs TortoiseGit's
 * ChangedDlg, which is not ported yet.
 */
class DiffCommand : public Command
{
public:
    DiffCommand();

    [[nodiscard]] bool execute() override;

    /**
     * Ported from DiffCommand::Execute: decides what to compare.
     * @param path The file, absolute.
     * @param path2 A second file to compare it with, absolute; empty for none.
     * @param startRev The left-hand revision; ignored unless @p endRev is given too.
     * @param endRev The right-hand revision; ignored unless @p startRev is given too.
     * @returns The git difftool to run.
     * @throws RuntimeError (category Git) if git fails, such as for a file outside a working tree.
     */
    [[nodiscard]] static DiffInvocation invocation(const QString& path, const QString& path2, const QString& startRev, const QString& endRev);
};

} // namespace kgt
