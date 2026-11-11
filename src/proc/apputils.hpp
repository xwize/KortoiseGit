// SPDX-FileCopyrightText: 2003-2011, 2013-2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/AppUtils.h (CAppUtils): helpers shared by several dialogs. Only what
 * has been needed so far is here.
 */

#pragma once

#include <KConfigGroup>

#include <QByteArray>
#include <QIcon>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace kgt
{

class Git;
class GitCall;
enum class ResetType;
struct ConflictHashes;
struct CreateBranchTagOptions;
struct CreateBranchTagParams;
struct GitSwitchOptions;
struct RebaseOptions;
struct StashSaveOptions;

/**
 * The titles and hashes of the two sides of a conflict, as CAppUtils::GetConflictTitles fills them in.
 */
struct ConflictTitles {
    QString mineText; // Our side
    QString mineHash; // Set only for a rebase found through rebase-apply/onto
    QString theirsText; // Their side
    QString theirsHash; // Empty during a rebase
};

/**
 * The arguments of CAppUtils::PerformSwitch: what git checkout switches to and how.
 */
struct SwitchOptions {
    QString ref; // The branch, tag or commit to switch to
    bool force = false; // -f
    QString newBranch; // The branch to create, empty for none
    bool branchOverride = false; // -B in place of -b
    Qt::CheckState track = Qt::PartiallyChecked; // --track, --no-track or neither, with a new branch only
    bool merge = false; // --merge
};

/**
 * What CAppUtils::ConflictEdit does with an unmerged file.
 */
enum class ConflictKind {
    Content, // Both sides have the file: a merge tool can resolve it
    DeleteModify, // One side has no such file: DeleteConflictDlg asks which side wins
    Submodule, // A side is a submodule: CSubmoduleResolveConflictDlg, not ported yet
};

/**
 * How a git stash apply or git stash pop ended.
 */
enum class StashOutcome {
    Success,
    Conflicts, // The stash was restored, leaving conflicts to resolve
    Failed,
};

/**
 * Helpers shared by several dialogs and commands, TortoiseGit's CAppUtils.
 */
class AppUtils
{
public:
    using IsInstalled = std::function<bool(const QString&)>; // Says whether an executable is on the PATH

    AppUtils() = delete;

    /**
     * Registers icons.qrc, which a static library has to do itself.
     */
    static void initIconResources();

    /**
     * @returns Where TortoiseGit's top-level registry values (Software\\TortoiseGit\\...) are kept.
     */
    [[nodiscard]] static KConfigGroup generalSettings();

    /**
     * @param dialog A dialog's name, such as "ResetDlg".
     * @returns Where its size is kept, TortoiseGit's TortoiseProc\\ResizableState.
     */
    [[nodiscard]] static KConfigGroup resizableState(const QString& dialog);

    /**
     * @returns The group holding every dialog's size.
     */
    [[nodiscard]] static KConfigGroup allResizableStates();

    /**
     * Ported from CAppUtils::LaunchRemoteSetting: shows the Settings dialog on its Remote page until it is closed.
     * @param workingTree The working tree's root or the bare repository.
     * @param parent Parent of the dialog.
     */
    static void launchRemoteSetting(const QString& workingTree, QWidget *parent = nullptr);

    /**
     * @param call A finished git call that lists branches or tags.
     * @returns Its output, or nothing if git failed: TortoiseGit's loading threads ignore these lists' errors too.
     * @throws RuntimeError for a failure other than git's.
     */
    [[nodiscard]] static QByteArray outputOrEmpty(const GitCall& call);

    /**
     * @returns The app icon: the installed hicolor theme's, or the copy bundled in icons.qrc when running from build/bin.
     */
    [[nodiscard]] static QIcon appIcon();

    /**
     * @param text What to put on the clipboard.
     */
    static void copyToClipboard(const QString& text);

    /**
     * Ported from CAppUtils::GetClipboardLink: a repository URL on the clipboard, for prefilling a dialog.
     * @param skipGitPrefix A command such as "git clone " that may precede the URL, to be skipped.
     * @param paramsCount How many words after the prefix to keep.
     * @returns The URL, or an empty string if the clipboard holds none.
     */
    [[nodiscard]] static QString getClipboardLink(const QString& skipGitPrefix = {}, int paramsCount = 0);

    /**
     * getClipboardLink()'s rules applied to a given text.
     * @param clipboardText The clipboard's text.
     * @param skipGitPrefix A command such as "git clone " that may precede the URL, to be skipped.
     * @param paramsCount How many words after the prefix to keep.
     * @returns The URL, or an empty string if the text holds none.
     */
    [[nodiscard]] static QString clipboardLink(QString clipboardText, const QString& skipGitPrefix = {}, int paramsCount = 0);

    /**
     * Ported from CAppUtils::RunTortoiseGitProc: starts another kortoisegitproc, detached.
     * @param arguments Its arguments, such as --command log --path /repo.
     * @returns Whether it started.
     */
    static bool runTortoiseGitProc(const QStringList& arguments);

    /**
     * Ported from CAppUtils::IsTGitRebaseActive: whether a TortoiseGit rebase holds its lock (a tgitrebase.active
     * directory in the admin directory). If it does, asks whether to remove it as stale.
     * @param workingTree The working tree.
     * @param parent Parent of the question.
     * @returns True if the lock stays, so the caller must not go on.
     */
    [[nodiscard]] static bool isTGitRebaseActive(const QString& workingTree, QWidget *parent = nullptr);

    /**
     * Shows IDS_NEED_TO_RESOLVE_CONFLICTS_HINT after a merge or pull stopped on conflicts, unless the user asked
     * not to see it again ("MergeConflictsNeedsCommit").
     * @param parent Parent of the message.
     */
    static void showMergeConflictsHint(QWidget *parent);

    /**
     * The git command of CAppUtils's static Reset.
     * @param type How to reset; Merge ignores @p resetTo, as "git reset --merge" resets to HEAD.
     * @param resetTo The commit to reset to.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList resetArguments(ResetType type, const QString& resetTo);

    /**
     * Ported from CAppUtils's static Reset, without its post-command buttons: runs the reset in ProgressDlg.
     * @param workingTree The working tree.
     * @param type How to reset.
     * @param resetTo The commit to reset to.
     * @param parent Parent of the progress dialog.
     * @returns True if the reset succeeded and the dialog was closed with Close.
     */
    static bool reset(const QString& workingTree, ResetType type, const QString& resetTo, QWidget *parent = nullptr);

    /**
     * Ported from CAppUtils::GitReset: asks what to reset to and how in ResetDlg, then resets.
     * @param workingTree The working tree's root or the bare repository.
     * @param ref What the dialog starts at.
     * @param parent Parent of the dialogs.
     * @returns True if the reset succeeded.
     */
    static bool gitReset(const QString& workingTree, const QString& ref, QWidget *parent = nullptr);

    /**
     * Ported from CAppUtils::MergeAbort: asks how in MergeAbortDlg, then resets to HEAD.
     * @param workingTree The working tree.
     * @param parent Parent of the dialogs.
     * @returns True if the merge was aborted.
     */
    static bool mergeAbort(const QString& workingTree, QWidget *parent = nullptr);

    /**
     * CAppUtils::StashSave's command line.
     * @param options What the user chose in StashSaveDlg.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList stashSaveArguments(const StashSaveOptions& options);

    /**
     * Ported from CAppUtils::StashSave, without its post-command buttons or CheckUserData: asks how in StashSaveDlg,
     * then runs git stash push in ProgressDlg.
     * @param git Runs git in the working tree's root.
     * @param message The message the dialog starts with.
     * @param parent Parent of the dialogs.
     * @returns True if git succeeded and the progress dialog was closed with Close.
     */
    static bool stashSave(const Git& git, const QString& message = {}, QWidget *parent = nullptr);

    /**
     * The decision shared by CAppUtils::StashApply and StashPop.
     * @param exitCode git's exit code.
     * @param output What git wrote, stdout and stderr.
     * @returns How the command ended: exit code 1 with a CONFLICT in the output is not a failure.
     */
    [[nodiscard]] static StashOutcome stashOutcome(int exitCode, const QString& output);

    /**
     * CAppUtils::StashApply's command line.
     * @param ref The stash, such as refs/stash@{1} or stash{1}; empty for the latest.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList stashApplyArguments(const QString& ref);

    /**
     * Ported from CAppUtils::StashApply: runs git stash apply without blocking, then reports how it went.
     * @param git Runs git in the working tree's root.
     * @param ref The stash to apply; empty for the latest.
     * @param parent Parent of the windows.
     * @returns False if git failed; true if it applied the stash, even with conflicts.
     */
    static bool stashApply(const Git& git, const QString& ref = {}, QWidget *parent = nullptr);

    /**
     * Ported from CAppUtils::StashPop: runs git stash pop without blocking, then reports how it went.
     * @param workingTree The working tree's root.
     * @param parent Parent of the windows.
     * @returns False if git failed; true if it restored the stash, even with conflicts.
     */
    static bool stashPop(const QString& workingTree, QWidget *parent = nullptr);

    /**
     * Ported from CAppUtils::Switch: asks what to switch to in GitSwitchDlg, then switches.
     * @param workingTree The working tree's root.
     * @param initialRefName What the dialog selects first; empty for the current branch.
     * @param parent Parent of the dialogs.
     * @returns True if the switch succeeded.
     */
    static bool switchTo(const QString& workingTree, const QString& initialRefName = {}, QWidget *parent = nullptr);

    /**
     * The part of CAppUtils::Switch between the dialog and PerformSwitch. A branch keeps its refs/heads/ prefix
     * only with "Override branch if exists", since checking out refs/heads/<name> would detach HEAD.
     * @param chosen What the user chose in GitSwitchDlg.
     * @returns PerformSwitch's arguments.
     */
    [[nodiscard]] static SwitchOptions switchOptions(const GitSwitchOptions& chosen);

    /**
     * PerformSwitch's command line.
     * @param options What to switch to and how; options.ref must already be FixBranchName'd.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList switchArguments(const SwitchOptions& options);

    /**
     * Ported from CAppUtils::PerformSwitch, without its post-command buttons: runs git checkout in ProgressDlg.
     * A switch with --merge that leaves conflicts counts as a failure.
     * @param workingTree The working tree's root.
     * @param options What to switch to and how.
     * @param parent Parent of the progress dialog.
     * @returns True if the switch succeeded and the dialog was closed with Close.
     */
    static bool performSwitch(const QString& workingTree, const SwitchOptions& options, QWidget *parent = nullptr);

    /**
     * Ported from CAppUtils::CreateBranchTag: asks how in CreateBranchTagDlg and runs git branch or git tag. A new
     * branch is then switched to if asked to and gets its description; a new tag is offered for pushing if asked to.
     * @param workingTree The working tree's root or the bare repository.
     * @param params What the dialog starts with.
     * @param parent Parent of the dialogs.
     * @returns True if the branch or tag was created, whatever became of the push.
     */
    static bool createBranchTag(const QString& workingTree, const CreateBranchTagParams& params, QWidget *parent = nullptr);

    /**
     * CAppUtils::CreateBranchTag's command line for a branch.
     * @param options What the user chose in CreateBranchTagDlg; options.versionName must already be FixBranchName'd.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList createBranchArguments(const CreateBranchTagOptions& options);

    /**
     * CAppUtils::CreateBranchTag's command line for a tag.
     * @param options What the user chose in CreateBranchTagDlg; options.versionName must already be FixBranchName'd
     *                and options.message trimmed. An empty message makes a lightweight tag.
     * @param messageFile The file holding the message; ignored without a message.
     * @returns The arguments after "git".
     */
    [[nodiscard]] static QStringList createTagArguments(const CreateBranchTagOptions& options, const QString& messageFile);

    /**
     * Ported from CAppUtils::Push: asks what to push in PushDlg and pushes it.
     * @param workingTree The working tree's root or the bare repository.
     * @param selectLocalBranch The local branch or ref to preselect; empty for the current branch.
     * @param parent Parent of the dialogs.
     * @returns True if the push ran and succeeded.
     */
    static bool push(const QString& workingTree, const QString& selectLocalBranch = {}, QWidget *parent = nullptr);

    /**
     * Ported from CAppUtils::RebaseAfterFetch, without the post-rebase buttons (Show log, Push, Send mail, Rebase),
     * which RebaseDlg doesn't have yet: opens RebaseDlg.
     * @param workingTree The working tree's root.
     * @param options The upstream to rebase onto, and whether to start at once and preserve merges.
     * @param parent Parent of the dialog.
     * @returns True if the rebase finished.
     */
    static bool rebaseAfterFetch(const QString& workingTree, const RebaseOptions& options, QWidget *parent = nullptr);

    /**
     * Ported from CAppUtils::DeleteRef: asks, by the kind of ref, what to do and deletes it.
     * @param git Runs git in the working tree's root or the bare repository.
     * @param ref The full ref name.
     * @param parent Parent of the dialogs.
     * @returns False if the user refused or deleting failed locally, so a caller going through several refs stops.
     *          A stash or remote branch whose git command fails counts as handled, as in TortoiseGit.
     */
    static bool deleteRef(const Git& git, const QString& ref, QWidget *parent = nullptr);

    /**
     * Ported from CAppUtils::UpdateBranchDescription: sets branch.<branch>.description, or removes it if the
     * description is blank. Like TortoiseGit it carries on if git refuses, which is only logged.
     * @param git Runs git in the working tree's root or the bare repository.
     * @param branch The branch.
     * @param description The description.
     * @returns False if no branch was given.
     */
    static bool updateBranchDescription(const Git& git, const QString& branch, QString description);

    /**
     * Ported from CTGitPath::IsRebaseActive.
     * @param workingTree The working tree.
     * @returns Whether a rebase is in progress there (a rebase-apply or tgitrebase.active directory).
     */
    [[nodiscard]] static bool isRebaseActive(const QString& workingTree);

    /**
     * Ported from CAppUtils::GetConflictTitles.
     * @param git Runs git at the working tree's root.
     * @param rebaseActive Whether a rebase is in progress.
     * @returns The titles of the two sides of a conflict.
     * @throws RuntimeError (category Git) if git fails.
     */
    [[nodiscard]] static ConflictTitles getConflictTitles(const Git& git, bool rebaseActive);

    /**
     * The choice CAppUtils::ConflictEdit makes from the stages of an unmerged file.
     * @param hashes The file's stages.
     * @returns What to do with it.
     */
    [[nodiscard]] static ConflictKind conflictKind(const ConflictHashes& hashes);

    /**
     * @returns The merge tools mergeToolArguments() chooses from, as git names them, the preferred one first.
     */
    [[nodiscard]] static QStringList mergeTools();

    /**
     * The command line that opens a content conflict in a merge tool. git asks before starting a tool it guessed,
     * even with --no-prompt, and with no terminal it gets no answer, so without merge.tool the tool is named here.
     * @param configuredTool The value of merge.tool; empty if it isn't set.
     * @param isInstalled Says whether an executable is on the PATH.
     * @returns The arguments after "git", without the file, or an empty list if merge.tool isn't set and no supported
     *          tool is installed.
     */
    [[nodiscard]] static QStringList mergeToolArguments(const QString& configuredTool, const IsInstalled& isInstalled);

    /**
     * Ported from CAppUtils::ConflictEdit: opens a content conflict in git mergetool, and a delete/modify conflict in
     * DeleteConflictDlg. Errors are shown to the user.
     * @param git Runs git at the working tree's root.
     * @param path The unmerged file, relative to the root.
     * @param parent Parent of the dialogs.
     * @returns True if DeleteConflictDlg ran to its end, so the caller should reload the file's status.
     */
    static bool conflictEdit(const Git& git, const QString& path, QWidget *parent = nullptr);
};

} // namespace kgt
