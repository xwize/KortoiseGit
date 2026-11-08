// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/FetchCommand.h.
 */

#pragma once

#include "commands/command.hpp"

#include <QStringList>

namespace kgt
{

struct PullFetchOptions;

/**
 * Fetches ("fetch"): asks where from in PullFetchDlg, then runs git fetch in ProgressDlg, as CAppUtils::Fetch
 * and DoFetch do. Works in a bare repository too. Several paths are fetched one after another, each of which must
 * be a working tree's root or a bare repository.
 */
class FetchCommand : public Command
{
public:
    FetchCommand();

    /**
     * Shows PullFetchDlg in fetch mode and, if accepted, the fetch's progress.
     * @returns True if every git fetch succeeded.
     */
    [[nodiscard]] bool execute() override;

    /**
     * Ported from DoFetch in CAppUtils.
     * @param options Where to fetch from and how.
     * @returns The arguments after "git" for the fetch.
     */
    [[nodiscard]] static QStringList fetchArguments(const PullFetchOptions& options);

private:
    /**
     * Ported from CAppUtils::Fetch and DoFetch, without the post-command buttons.
     * @param repository The working tree's root or the bare repository.
     * @returns True if git fetch succeeded.
     */
    [[nodiscard]] static bool fetch(const QString& repository);
};

} // namespace kgt
