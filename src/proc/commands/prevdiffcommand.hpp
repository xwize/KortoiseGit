// SPDX-FileCopyrightText: 2008-2011, 2016, 2018-2019, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/PrevDiffCommand.h.
 */

#pragma once

#include "commands/command.hpp"
#include "gitdiff.hpp"

#include <optional>

namespace kgt
{

/**
 * Compares a file in the working tree with its version before the last commit that touched it, in git difftool.
 * A folder needs TortoiseGit's FileDiffDlg, which is not ported yet.
 */
class PrevDiffCommand : public Command
{
public:
    PrevDiffCommand();

    [[nodiscard]] bool execute() override;

    /**
     * Ported from PrevDiffCommand::Execute: finds the revision to compare with.
     * @param path The file, absolute.
     * @returns The git difftool to run, or nothing if fewer than two commits touched the file.
     * @throws RuntimeError (category Git) if git can't find the working tree.
     */
    [[nodiscard]] static std::optional<DiffInvocation> invocation(const QString& path);
};

} // namespace kgt
