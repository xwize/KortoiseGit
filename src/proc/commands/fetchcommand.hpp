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

class QWidget;

namespace kgt
{

class Git;
struct PullFetchOptions;

/**
 * DoFetch's runRebase: what follows a successful fetch.
 */
enum class FetchRebase {
    None, // 0: nothing
    Open, // 1: open RebaseDlg, after asking if there seems to be nothing to rebase
    AutoStart, // 2: open RebaseDlg and start it; branch.<name>.rebase or pull.rebase asked for the rebase
};

/**
 * The branch a fetch with a rebase is about to update, as DoFetch notes it before fetching.
 */
struct FetchedUpstream {
    QString upstream; // What to rebase onto: remotes/<remote>/<branch>, FETCH_HEAD, or empty if it isn't known
    QString oldHash; // Its commit before the fetch; empty if it had none
};

/**
 * Fetches ("fetch"): asks where from in PullFetchDlg, then runs git fetch in ProgressDlg, as CAppUtils::Fetch
 * and DoFetch do, and opens RebaseDlg afterwards if asked to. Works in a bare repository too. Several paths are
 * fetched one after another, each of which must be a working tree's root or a bare repository.
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

    /**
     * Ported from DoFetch's choice of the upstream to rebase onto: a named remote's tracking branch of the branch
     * fetched, or of the current branch's upstream if none is named; FETCH_HEAD for a URL or "- all -".
     * @param git Runs git in the repository.
     * @param options Where to fetch from.
     * @returns The upstream and its commit before the fetch.
     */
    [[nodiscard]] static FetchedUpstream rebaseUpstream(const Git& git, const PullFetchOptions& options);

    /**
     * Ported from DoFetch's m_PostExecCallback: after a successful fetch, opens RebaseDlg on the upstream. Unless
     * the configuration asked for the rebase, it asks first when the current branch is up to date or the remote
     * branch didn't change, and offers to merge instead when the upstream fast-forwards.
     * @param git Runs git in the working tree's root.
     * @param fetched What rebaseUpstream() returned before the fetch.
     * @param runRebase Open or AutoStart.
     * @param preserveMerges Whether "Preserve merges" starts checked.
     * @param parent Parent of the dialogs.
     */
    static void rebaseFetched(const Git& git, const FetchedUpstream& fetched, FetchRebase runRebase, bool preserveMerges, QWidget *parent = nullptr);

    /**
     * Ported from DoFetch in CAppUtils, without the post-command buttons: runs git fetch in ProgressDlg, then
     * rebases if asked to.
     * @param repository The working tree's root or the bare repository.
     * @param options Where to fetch from and how.
     * @param runRebase What follows a successful fetch.
     * @param parent Parent of the dialogs.
     * @returns True if git fetch succeeded.
     */
    static bool doFetch(const QString& repository, const PullFetchOptions& options, FetchRebase runRebase, QWidget *parent = nullptr);

private:
    /**
     * Ported from CAppUtils::Fetch.
     * @param repository The working tree's root or the bare repository.
     * @returns True if git fetch succeeded.
     */
    [[nodiscard]] static bool fetch(const QString& repository);
};

} // namespace kgt
