// SPDX-FileCopyrightText: 2008-2009, 2015-2016, 2018-2019, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/TagCommand.h.
 */

#pragma once

#include "commands/command.hpp"

namespace kgt
{

/**
 * Creates a tag ("tag"): asks how in CreateBranchTagDlg, then runs git tag, as CAppUtils::CreateBranchTag does. --rev
 * preselects what to base it on and --name fills in its name.
 */
class TagCommand : public Command
{
public:
    TagCommand();

    /**
     * Shows CreateBranchTagDlg in tag mode and, if accepted, creates the tag.
     * @returns True if the tag was created.
     */
    [[nodiscard]] bool execute() override;
};

} // namespace kgt
