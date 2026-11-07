// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/PushCommand.h.
 */

#pragma once

#include "commands/command.hpp"

#include <QStringList>

namespace kgt
{

struct PushOptions;

/**
 * Pushes ("push"): asks what to push in PushDlg, then runs git push in ProgressDlg, as CAppUtils::Push and
 * CAppUtils::DoPush do.
 */
class PushCommand : public Command
{
public:
    PushCommand();

    /**
     * Shows PushDlg and, if accepted, the push's progress.
     * @returns True if git push succeeded.
     */
    [[nodiscard]] bool execute() override;

    /**
     * Ported from CAppUtils::DoPush.
     * @param options What to push where.
     * @returns The arguments after "git" for the push.
     */
    [[nodiscard]] static QStringList pushArguments(const PushOptions& options);
};

} // namespace kgt
