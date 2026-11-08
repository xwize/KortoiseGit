// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/RebaseDlg.cpp: the rebase itself (OnBnClickedContinue, RebaseThread,
 * StartRebase, DoRebase, FinishRebase and OnBnClickedAbort's cleanup), with TortoiseGit's git command sequences.
 * Everything here except the UI helpers at the end runs on the worker thread.
 */

#include "rebasedlg.hpp"

#include "apputils.hpp"
#include "commitdlg.hpp"
#include "gitloglist.hpp"
#include "gitloglistmodel.hpp"
#include "gitstatuslistctrl.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QCheckBox>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QThread>

#include <algorithm>
#include <utility>

namespace
{

using namespace kgt;

// The empty tree of a SHA-1 repository, which IsCommitEmpty compares a root commit with
constexpr auto emptyTreeHash = "4b825dc642cb6eb9a060e54bf8d69288fbee4904";

/**
 * CRebaseDlg::m_iSquashdate (the SquashDate setting): 0 keeps the first commit's date, 1 the last one's, 2 takes
 * the current date. Not configurable yet; TortoiseGit's default.
 */
constexpr int squashDate = 0;

/**
 * @param hash A full commit hash or anything else.
 * @returns Whether it is a full hexadecimal SHA-1 or SHA-256 hash, which needs no lookup (CGit::GetHash).
 */
bool isFullHash(const QString& hash)
{
    if (hash.size() != 40 && hash.size() != 64) {
        return false;
    }
    return std::ranges::all_of(hash, [](QChar digit) {
        return (digit >= QLatin1Char('0') && digit <= QLatin1Char('9')) || (digit >= QLatin1Char('a') && digit <= QLatin1Char('f'));
    });
}

/**
 * @param value A git configuration value.
 * @returns Whether git reads it as true.
 */
bool isTrue(const QString& value)
{
    const QString lower = value.toLower();
    return lower == QLatin1String("true") || lower == QLatin1String("yes") || lower == QLatin1String("on") || lower == QLatin1String("1");
}

/**
 * @param arguments Git's arguments.
 * @returns The command line for the log, as TortoiseGit writes it.
 */
QString commandLine(const QStringList& arguments)
{
    return QStringLiteral("git ") + arguments.join(QLatin1Char(' '));
}

} // namespace

namespace kgt
{

void RebaseDlg::continueJob()
{
    const ContinueResult result = continueRebase();
    if (result == ContinueResult::Close) {
        m_closeAfterThread = true;
        m_acceptAfterThread = true;
        return;
    }
    if (result == ContinueResult::RunRebase) {
        rebaseThread();
    }
}

RebaseDlg::ContinueResult RebaseDlg::continueRebase()
{
    // OnBnClickedContinue; Done is dealt with on the UI thread
    if (m_rebaseStage == RebaseStage::ChooseBranch) {
        bool active = false;
        runOnUi([this, &active] {
            active = AppUtils::isTGitRebaseActive(m_git.workingDirectory(), this);
        });
        if (active || !checkRebaseCondition()) {
            return ContinueResult::Stop;
        }
    }

    m_abortRequested = false;
    if (m_isFastForward) {
        std::ignore = fastForward();
        return ContinueResult::Stop;
    }

    if (m_rebaseStage == RebaseStage::ChooseBranch) {
        m_rebaseStage = RebaseStage::Start;
        runOnUi([this] {
            m_fileListCtrl->clear();
            m_fileListCtrl->setHasCheckboxes(false);
            m_fileListCtrl->setCurrentVersion({}, {});
            m_tabs->setTabText(ConflictTab, i18n("Conflict Files"));
            m_tabs->insertTab(LogTab, m_outputRebase, i18n("Log"));
            m_tabs->setCurrentIndex(LogTab);
        });
    }

    if (m_rebaseStage == RebaseStage::Finish) {
        return finishRebase() ? ContinueResult::Close : ContinueResult::Stop;
    }
    if (m_rebaseStage == RebaseStage::SquashConflict && !continueSquashConflict()) {
        return ContinueResult::Stop;
    }
    if (m_rebaseStage == RebaseStage::Conflict && !continueConflict()) {
        return ContinueResult::Stop;
    }
    if (!continueNotClean()) {
        return ContinueResult::Stop;
    }
    if ((m_rebaseStage == RebaseStage::Edit || m_rebaseStage == RebaseStage::SquashEdit) && !continueEdit()) {
        return ContinueResult::Stop;
    }
    return ContinueResult::RunRebase;
}

bool RebaseDlg::checkRebaseCondition()
{
    // CheckRebaseCondition. TortoiseGit's CheckUserData and pre-rebase hook are not ported.
    setActiveTab(LogTab);
    if (checkCleanWorkTree()) {
        return true;
    }
    if (!isTrue(m_git.configValue(QStringLiteral("rebase.autostash")))) {
        const int choice =
            askUser(QMessageBox::Question, i18n("The current working tree is not clean.\nDo you want to stash the changes?"), {i18n("&Stash"), i18n("A&bort")});
        if (choice != 1) {
            return false;
        }
    }
    addLogString(QStringLiteral("git stash"));
    const GitResult stash = gitRun({QStringLiteral("stash")});
    if (!stash.ok) {
        showError(stash.output);
        return false;
    }
    m_stashed = true;
    return true;
}

bool RebaseDlg::fastForward()
{
    // OnBnClickedContinue's m_IsFastForward part
    m_origBranchHash = hashOf(m_branch);
    if (m_origBranchHash.isEmpty()) {
        showError(i18n("Could not get hash of \"%1\".", m_branch));
        return false;
    }
    m_origUpstreamHash = hashOf(m_upstream);
    if (m_origUpstreamHash.isEmpty()) {
        showError(i18n("Could not get hash of \"%1\".", m_upstream));
        return false;
    }
    if (!isFastForward(m_branch, m_upstream)) {
        setActiveTab(LogTab);
        addLogString(QStringLiteral("No fast forward possible.\nMaybe repository changed"));
        return false;
    }
    if (isLocalBranch(m_branch)) {
        const QStringList checkout{QStringLiteral("checkout"),
                                   QStringLiteral("--no-track"),
                                   QStringLiteral("-f"),
                                   QStringLiteral("-B"),
                                   m_branch,
                                   QStringLiteral("--end-of-options"),
                                   m_upstream,
                                   QStringLiteral("--")};
        addLogString(commandLine(checkout));
        if (!runGitCmdRetryOrAbort(checkout)) {
            return false;
        }
    }
    const QStringList reset{QStringLiteral("reset"),
                            QStringLiteral("--hard"),
                            QStringLiteral("--end-of-options"),
                            m_git.fixBranchName(m_upstream),
                            QStringLiteral("--")};
    addLogString(i18n("Fast forward to \"%1\"", m_upstream));
    addLogString(commandLine(reset));
    setActiveTab(LogTab);
    if (!runGitCmdRetryOrAbort(reset)) {
        return false;
    }
    addLogString(i18n("Done"));
    m_rebaseStage = RebaseStage::Done;
    runOnUi([this] {
        updateCurrentStatus();
    });
    return true;
}

bool RebaseDlg::continueSquashConflict()
{
    // OnBnClickedContinue at Squash_Conclict. TortoiseGit's MessageContainsConflictHints is left out: the
    // message comes from the commit, not from git's MERGE_MSG, so it never holds git's conflict hints.
    if (!verifyNoConflict()) {
        return false;
    }
    const int row = m_currentRebaseIndex;
    if (!isNextCommitSquash()) {
        m_rebaseStage = RebaseStage::SquashEdit;
        runOnUi([this] {
            onRebaseUpdateUi();
        });
        return false;
    }
    m_rebaseStage = RebaseStage::Continue;
    addRebaseAction(row, GitRevLoglist::RebaseDone);
    m_forRewrite.append(currentRev().hash);
    runOnUi([this] {
        updateCurrentStatus();
    });
    return true;
}

bool RebaseDlg::continueConflict()
{
    // OnBnClickedContinue at Conclict: commit the resolved files with the picked commit's message
    if (!verifyNoConflict()) {
        return false;
    }
    setActiveTab(LogTab);
    const GitRevLoglist rev = currentRev();

    // ATTENTION: similar code in CommitDlg (CommitDlg::planCommit is that code)
    CommitPlan plan;
    runOnUi([this, &plan] {
        plan = CommitDlg::planCommit(*m_fileListCtrl->statusModel());
    });
    for (const CommitStep& step : std::as_const(plan.prepareIndex)) {
        if (!gitRun(step.arguments).ok && !step.ignoreErrors) {
            addLogString(QStringLiteral("An error occurred while updating the index."));
            return false;
        }
    }
    const QString message = m_messageText.trimmed();
    if (!commitResolvedConflict(rev, message)) {
        return false;
    }
    // ReaddUnselectedAddedFilesAfterCommit
    for (const CommitStep& step : std::as_const(plan.restoreIndex)) {
        std::ignore = gitRun(step.arguments);
    }

    const int row = m_currentRebaseIndex;
    if (rev.rebaseAction & GitRevLoglist::RebaseEdit) {
        m_rebaseStage = RebaseStage::Edit;
        runOnUi([this] {
            m_tabs->setCurrentIndex(MessageTab);
            updateCurrentStatus();
        });
        return false;
    }
    m_rebaseStage = RebaseStage::Continue;
    addRebaseAction(row, GitRevLoglist::RebaseDone);
    runOnUi([this] {
        updateCurrentStatus();
    });
    if (isNextCommitSquash()) {
        // Remember the commit message after editing if the next commit is a squash
        resetParentForSquash(message);
        return true;
    }
    m_squashMessage.clear();
    const QString head = hashOf(QStringLiteral("HEAD"));
    if (head.isEmpty()) {
        showError(i18n("Could not get HEAD hash."));
        return false;
    }
    m_rewrittenCommitsMap.insert(rev.hash, head);
    return true;
}

bool RebaseDlg::commitResolvedConflict(const GitRevLoglist& rev, const QString& message)
{
    EmptyCommit empty;
    if (!m_currentCommitEmpty) {
        const std::optional<EmptyCommit> choice = checkResultingCommitEmpty(false);
        if (!choice) {
            return false;
        }
        empty = *choice;
    }
    const bool allowEmpty = empty.allowEmpty;
    const bool skipCurrent = empty.skip;
    QStringList commit{QStringLiteral("commit")};
    if (allowEmpty) {
        commit.append(QStringLiteral("--allow-empty"));
    }
    commit.append({QStringLiteral("--allow-empty-message"), QStringLiteral("-C"), rev.hash});
    addLogString(commandLine(commit));
    if (!skipCurrent) {
        const GitResult result = gitRun(commit);
        addLogString(result.output);
        if (!result.ok) {
            showError(result.output);
            return false;
        }
    }

    // Update the commit message if needed
    if (skipCurrent || message == QString(rev.subject + QLatin1Char('\n') + rev.body).trimmed()) {
        return true;
    }
    if (message.isEmpty()) {
        showError(i18n("The commit message must not be empty."));
        return false;
    }
    const QStringList amend{QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("-F"), QStringLiteral("-")};
    addLogString(commandLine(amend));
    const GitResult result = gitRun(amend, message.toUtf8());
    addLogString(result.output);
    if (!result.ok) {
        if (!checkCleanWorkTree()) {
            showError(result.output);
            return false;
        }
        if (askUser(QMessageBox::Critical, result.output, {i18n("&Retry"), i18n("&Ignore")}) == 1) {
            return false;
        }
    }
    return true;
}

std::optional<RebaseDlg::EmptyCommit> RebaseDlg::checkResultingCommitEmpty(bool amend)
{
    // CGit::IsResultingCommitBecomeEmpty and the question TortoiseGit asks about it; nullopt cancels
    const QString base = amend ? QStringLiteral("HEAD~1") : QStringLiteral("HEAD");
    if (!gitRun({QStringLiteral("diff-index"), QStringLiteral("--cached"), QStringLiteral("--quiet"), base, QStringLiteral("--")}).ok) {
        return EmptyCommit{};
    }
    // In the conflict stage, an empty commit followed by a squash needs no question
    if (m_rebaseStage == RebaseStage::Conflict && isNextCommitSquash()) {
        m_currentCommitEmpty = false;
        return EmptyCommit{.allowEmpty = true, .skip = false};
    }
    const int choice = askUser(QMessageBox::Question,
                               i18n("The current commit will be empty (e.g., due to conflict resolution). Skip the commit or keep the message only commit?"),
                               {i18n("C&ommit"), i18n("&Skip"), i18n("Cancel")});
    if (choice == 2) {
        return EmptyCommit{.allowEmpty = false, .skip = true};
    }
    if (choice == 1) {
        m_currentCommitEmpty = true;
        return EmptyCommit{.allowEmpty = true, .skip = false};
    }
    return std::nullopt;
}

bool RebaseDlg::continueNotClean()
{
    /**
     * OnBnClickedContinue: unstaged changes when the next commit is no squash. TortoiseGit asks, then reviews and
     * commits them in the commit dialog. Our commit dialog cannot amend yet, so they are staged (git add -u, what
     * the commit dialog commits by default) and go into the commit the stage makes: the amended Edit commit, the
     * squashed commit, or, after a resolved conflict, the commit just made.
     */
    const RebaseStage stage = m_rebaseStage;
    if ((stage != RebaseStage::Edit && stage != RebaseStage::Continue && stage != RebaseStage::SquashEdit) || isNextCommitSquash()
        || checkCleanWorkTree(true)) {
        return true;
    }
    const int choice = askUser(QMessageBox::Critical,
                               i18n("The working tree is not clean and contains unstaged changes.\nReview and commit the changes?"),
                               {i18n("OK"), i18n("A&bort")});
    if (choice != 1) {
        return false;
    }
    const QStringList add{QStringLiteral("add"), QStringLiteral("-u")};
    addLogString(commandLine(add));
    if (!runGitCmdRetryOrAbort(add)) {
        return false;
    }
    if (stage != RebaseStage::Continue) {
        return true;
    }
    const QStringList amend{QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("--no-edit")};
    addLogString(commandLine(amend));
    if (!runGitCmdRetryOrAbort(amend)) {
        return false;
    }
    const QString head = hashOf(QStringLiteral("HEAD"));
    m_rewrittenCommitsMap.insert(currentRev().hash, head);
    for (const QString& hash : std::as_const(m_forRewrite)) {
        m_rewrittenCommitsMap.insert(hash, head);
    }
    m_forRewrite.clear();
    return true;
}

bool RebaseDlg::continueEdit()
{
    // OnBnClickedContinue at Edit and Squash_Edit: amend the commit, or make the squashed one
    const GitRevLoglist rev = currentRev();
    const QString message = m_messageText;
    if (message.trimmed().isEmpty()) {
        showError(i18n("The commit message must not be empty."));
        return false;
    }
    const bool squashEdit = m_rebaseStage == RebaseStage::SquashEdit;
    EmptyCommit empty{.allowEmpty = m_currentCommitEmpty, .skip = false};
    if (!empty.allowEmpty) {
        const std::optional<EmptyCommit> choice = checkResultingCommitEmpty(!squashEdit);
        if (!choice) {
            return false;
        }
        empty = *choice;
    }
    const bool allowEmpty = empty.allowEmpty;
    const bool skipCurrent = empty.skip;
    QStringList commit{QStringLiteral("commit")};
    if (!squashEdit) {
        commit.append(QStringLiteral("--amend"));
    }
    if (allowEmpty) {
        commit.append(QStringLiteral("--allow-empty"));
    }
    if (squashEdit) {
        commit.append(m_squashFirstMetaData.asParams(squashDate == 2));
    }
    commit.append({QStringLiteral("-F"), QStringLiteral("-")});
    if (!skipCurrent) {
        const GitResult result = gitRun(commit, message.toUtf8());
        addLogString(result.output);
        if (!result.ok) {
            if (!checkCleanWorkTree()) {
                showError(result.output);
                return false;
            }
            if (askUser(QMessageBox::Critical, result.output, {i18n("&Retry"), i18n("&Ignore")}) == 1) {
                return false;
            }
        }
    }

    const bool nextIsSquash = isNextCommitSquash();
    if (nextIsSquash) {
        // Remember the commit message after editing if the next commit is a squash
        const std::optional<GitRevLoglist> latest = getCommit(QStringLiteral("HEAD"));
        if (!latest) {
            showError(i18n("Could not get HEAD commit."));
            return false;
        }
        m_squashFirstMetaData = {.name = latest->authorName, .email = latest->authorEmail, .time = latest->authorDate, .set = true};
        resetParentForSquash(message);
    } else {
        m_squashMessage.clear();
    }
    setActiveTab(LogTab);
    m_rebaseStage = RebaseStage::Continue;
    const QString head = hashOf(QStringLiteral("HEAD"));
    if (head.isEmpty()) {
        showError(i18n("Could not get HEAD hash."));
        return false;
    }
    m_rewrittenCommitsMap.insert(rev.hash, head);
    for (const QString& hash : std::as_const(m_forRewrite)) {
        m_rewrittenCommitsMap.insert(hash, head);
    }
    if (!nextIsSquash) {
        m_forRewrite.clear();
    }
    addRebaseAction(m_currentRebaseIndex, GitRevLoglist::RebaseDone);
    runOnUi([this] {
        updateCurrentStatus();
    });
    return true;
}

void RebaseDlg::rebaseThread()
{
    // RebaseThread
    while (!m_abortRequested) {
        const RebaseStage stage = m_rebaseStage;
        if (stage == RebaseStage::Start) {
            if (!startRebase()) {
                break;
            }
            m_rebaseStage = RebaseStage::Continue;
        } else if (stage == RebaseStage::Continue) {
            goNext();
            runOnUi([this] {
                onRebaseUpdateUi();
            });
            if (isEnd()) {
                m_rebaseStage = RebaseStage::Finish;
            } else if (doRebase() != 0) {
                break;
            }
        } else if (stage == RebaseStage::Finish) {
            // TortoiseGit's OnRebaseUpdateUI finishes the rebase here, on the UI thread
            std::ignore = finishRebase();
            m_rebaseStage = RebaseStage::Done;
            break;
        } else {
            break;
        }
        runOnUi([this] {
            onRebaseUpdateUi();
        });
    }
}

bool RebaseDlg::startRebase()
{
    // StartRebase
    const QString headBranch = m_git.currentBranch();
    // GetCurrentBranch(true): HEAD when detached
    m_origHeadBranch = headBranch == QLatin1String("(no branch)") ? QStringLiteral("HEAD") : headBranch;
    m_origHeadHash = hashOf(QStringLiteral("HEAD"));
    if (m_origHeadHash.isEmpty()) {
        addLogString(i18n("No HEAD found"));
        return false;
    }
    if (!gitRun({QStringLiteral("update-ref"), QStringLiteral("ORIG_HEAD"), m_origHeadHash}).ok) {
        addLogString(QStringLiteral("update ORIG_HEAD Fail"));
        return false;
    }
    const QString upstream = m_onto.isEmpty() ? m_upstream : m_onto;
    m_origUpstreamHash = hashOf(upstream);
    if (m_origUpstreamHash.isEmpty()) {
        showError(i18n("Could not get hash of \"%1\".", upstream));
        return false;
    }
    m_origBranchHash = hashOf(m_branch);
    if (m_origBranchHash.isEmpty()) {
        showError(i18n("Could not get hash of \"%1\".", m_branch));
        return false;
    }
    const QStringList checkout{QStringLiteral("checkout"), QStringLiteral("-f"), m_origUpstreamHash, QStringLiteral("--")};
    addLogString(commandLine(checkout));
    if (!runGitCmdRetryOrAbort(checkout)) {
        return false;
    }
    if (!checkCleanWorkTree()) {
        // This may happen when text transformations are performed and a file in history is not normalized
        addLogString(QStringLiteral("Unrecoverable error: Working tree is not clean after checkout."));
        m_rebaseStage = RebaseStage::Error;
        return false;
    }
    addLogString(i18n("Start Rebase") + QLatin1Char('\n'));
    return true;
}

void RebaseDlg::addToSquashMessage(const GitRevLoglist& rev)
{
    // DoRebase: the messages of a squash are joined, each after an empty line
    if (!m_squashMessage.isEmpty()) {
        m_squashMessage += QStringLiteral("\n\n");
    }
    QString body = rev.body;
    while (body.endsWith(QLatin1Char('\n')) || body.endsWith(QLatin1Char(' '))) {
        body.chop(1);
    }
    m_squashMessage += rev.subject + QLatin1Char('\n') + body;
}

int RebaseDlg::doRebase()
{
    // DoRebase
    if (m_currentRebaseIndex < 0 || m_currentRebaseIndex >= rowCount()) {
        return 0;
    }
    const int row = m_currentRebaseIndex;
    const GitRevLoglist rev = currentRev();
    unsigned int mode = rev.rebaseAction & GitRevLoglist::RebaseModeMask;
    if (mode == GitRevLoglist::RebaseSkip) {
        addRebaseAction(row, GitRevLoglist::RebaseDone);
        return 0;
    }
    const bool noCommit = prepareSquash(rev, mode);

    addLogString(QStringLiteral("%1 %2: %3").arg(GitLogListModel::rebaseActionName(mode)).arg(currentCommitId()).arg(rev.hash));
    addLogString(rev.subject);
    if (rev.subject.isEmpty()) {
        std::ignore = askUser(QMessageBox::Warning, i18n("Found an empty commit message. You have to enter one or rebase cannot proceed."), {i18n("OK")});
        mode = GitRevLoglist::RebaseEdit;
    }
    const std::optional<QStringList> options = pickOptions(rev.hash, mode, noCommit);
    if (!options) {
        return -1;
    }
    return runPick(row, rev, mode, *options);
}

bool RebaseDlg::prepareSquash(const GitRevLoglist& rev, unsigned int mode)
{
    // DoRebase: collect the squash message and the squashed commit's author
    const bool nextCommitIsSquash = isNextCommitSquash();
    if (nextCommitIsSquash || mode != GitRevLoglist::RebasePick) {
        addToSquashMessage(rev);
    } else {
        m_squashMessage.clear();
        m_squashFirstMetaData = {};
    }
    if (nextCommitIsSquash && mode != GitRevLoglist::RebaseSquash) {
        m_squashFirstMetaData = {.name = rev.authorName, .email = rev.authorEmail, .time = rev.authorDate, .set = true};
    }
    // The next or this commit is a squash (not on an edit->squash sequence): pick without committing
    const bool noCommit = (nextCommitIsSquash && mode != GitRevLoglist::RebaseEdit) || mode == GitRevLoglist::RebaseSquash;
    if (noCommit && squashDate == 1) {
        m_squashFirstMetaData.time = rev.authorDate;
    }
    return noCommit;
}

std::optional<QStringList> RebaseDlg::pickOptions(const QString& hash, unsigned int mode, bool noCommit)
{
    // DoRebase: git cherry-pick's options
    QStringList options;
    if (!noCommit) {
        // TortoiseGit issue #1833: if HEAD is the parent of the picked commit, fast-forward to it
        options.append(QStringLiteral("--ff"));
    }
    const int isEmpty = isCommitEmpty(hash);
    if (isEmpty < 0) {
        return std::nullopt;
    }
    if (isEmpty == 1) {
        options.append(QStringLiteral("--allow-empty"));
        if (mode != GitRevLoglist::RebaseSquash) {
            m_currentCommitEmpty = true;
        }
    } else {
        m_currentCommitEmpty = false;
    }
    if (noCommit) {
        options.append(QStringLiteral("--no-commit"));
    }
    return options;
}

int RebaseDlg::runPick(int row, const GitRevLoglist& rev, unsigned int mode, const QStringList& options)
{
    // DoRebase: pick, retrying if the user asks to
    const bool noCommit = options.contains(QStringLiteral("--no-commit"));
    while (true) {
        QStringList command = QStringList{QStringLiteral("cherry-pick")} + options + QStringList{rev.hash};
        if (m_preserveMerges && preserveMergesCommand(rev, mode, noCommit ? QStringLiteral("--no-commit") : QString(), command) != 0) {
            return -1;
        }
        const GitResult result = gitRun(command);
        if (!result.ok) {
            bool retry = false;
            const std::optional<int> handled = handlePickFailure(row, rev.hash, mode, result.output, retry);
            if (retry) {
                continue;
            }
            if (handled) {
                return *handled;
            }
        } else {
            addLogString(result.output);
        }
        return handlePickSuccess(row, rev.hash, mode, noCommit);
    }
}

QStringList RebaseDlg::rewrittenParents(const GitRevLoglist& rev, const QString& currentHead, bool& parentRewritten)
{
    // DoRebase with m_bPreserveMerges: the parents the rewritten commit gets
    QStringList possibleParents = rev.parents;
    QStringList newParents;
    while (!possibleParents.isEmpty()) {
        const QString parent = possibleParents.takeFirst();
        const auto rewritten = m_rewrittenCommitsMap.constFind(parent);
        if (rewritten == m_rewrittenCommitsMap.cend()) {
            const auto dropped = m_droppedCommitsMap.constFind(parent);
            if (dropped != m_droppedCommitsMap.cend()) {
                parentRewritten = true;
                possibleParents = *dropped + possibleParents;
                continue;
            }
            newParents.append(parent);
            continue;
        }
        if (rewritten->isEmpty() && parent == rev.parents.constFirst() && rev.parents.size() > 1) {
            m_rebaseStage = RebaseStage::Error;
            addLogString({});
            addLogString(QStringLiteral("Unrecoverable error: Merge commit parent missing."));
            return {};
        }
        // Use the current HEAD as fallback
        const QString newParent = rewritten->isEmpty() ? currentHead : *rewritten;
        if (newParent != parent) {
            parentRewritten = true;
        }
        if (!newParents.contains(newParent)) {
            newParents.append(newParent);
        }
    }
    return newParents;
}

int RebaseDlg::preserveMergesCommand(const GitRevLoglist& rev, unsigned int mode, const QString& noCommit, QStringList& command)
{
    // DoRebase with m_bPreserveMerges: check out the rewritten parent first, and re-create merges with git merge
    const QString currentHead = hashOf(QStringLiteral("HEAD"));
    if (currentHead.isEmpty()) {
        m_rebaseStage = RebaseStage::Error;
        showError(i18n("Could not get HEAD hash."));
        return -1;
    }
    for (const QString& commit : std::as_const(m_currentCommits)) {
        m_rewrittenCommitsMap.insert(commit, currentHead);
    }
    m_currentCommits = {rev.hash};
    bool parentRewritten = false;
    const QStringList newParents = rewrittenParents(rev, currentHead, parentRewritten);
    if (m_rebaseStage == RebaseStage::Error || newParents.isEmpty()) {
        return -1;
    }
    const bool isMerge = rev.parents.size() > 1;
    if (isMerge && mode == GitRevLoglist::RebaseSquash) {
        m_rebaseStage = RebaseStage::Error;
        addLogString(QStringLiteral("Cannot squash merge commit on rebase."));
        return -1;
    }
    if (isMerge && !parentRewritten && noCommit.isEmpty()) {
        command = {QStringLiteral("reset"), QStringLiteral("--hard"), rev.hash, QStringLiteral("--")};
        return 0;
    }
    if (!isMerge && mode == GitRevLoglist::RebaseSquash) {
        return 0;
    }
    if (!runGitCmdRetryOrAbort({QStringLiteral("checkout"), newParents.constFirst(), QStringLiteral("--")})) {
        m_rebaseStage = RebaseStage::Error;
        return -1;
    }
    if (!isMerge) {
        return 0;
    }
    command = QStringList{QStringLiteral("merge"), QStringLiteral("--no-ff")};
    if (!noCommit.isEmpty()) {
        command.append(noCommit);
    }
    command.append(QStringLiteral("--"));
    command.append(newParents);
    if (!noCommit.isEmpty()) {
        return 0;
    }
    const GitResult merge = gitRun(command);
    if (!merge.ok) {
        addLogString(commandLine(command));
        addLogString(merge.output);
        try {
            if (m_git.hasWorkingTreeConflicts()) {
                m_rebaseStage = RebaseStage::Conflict;
                return -1;
            }
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            addLogString(QStringLiteral("Checking for conflicts failed.\n") + e.error().message);
        }
        addLogString(QStringLiteral("An unrecoverable error occurred."));
        m_rebaseStage = RebaseStage::Error;
        return -1;
    }
    // Do nothing if already up to date (git merge says so again)
    if (hashOf(QStringLiteral("HEAD")) != currentHead) {
        command = {QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("-C"), rev.hash};
    }
    return 0;
}

std::optional<int> RebaseDlg::handlePickFailure(int row, const QString& hash, unsigned int mode, const QString& output, bool& retry)
{
    // DoRebase when git cherry-pick fails; nullopt goes on as after a successful pick
    addLogString(output);
    bool hasConflicts = false;
    try {
        hasConflicts = m_git.hasWorkingTreeConflicts();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        addLogString(QStringLiteral("Checking for conflicts failed.\n") + e.error().message);
        return -1;
    }
    if (hasConflicts) {
        m_rebaseStage = mode == GitRevLoglist::RebaseSquash ? RebaseStage::SquashConflict : RebaseStage::Conflict;
        return -1;
    }
    if (output.contains(QLatin1String("commit --allow-empty"))) {
        const int choice =
            askUser(QMessageBox::Question,
                    i18n("The current commit will be empty (e.g., due to conflict resolution). Skip the commit or keep the message only commit?"),
                    {i18n("C&ommit"), i18n("&Skip"), i18n("Cancel")});
        if (choice != 1) {
            if (choice == 2 && runGitCmdRetryOrAbort({QStringLiteral("reset"), QStringLiteral("--hard")})) {
                addRebaseAction(row, GitRevLoglist::RebaseDone);
                return 0;
            }
            m_rebaseStage = RebaseStage::Error;
            addLogString(QStringLiteral("An unrecoverable error occurred."));
            return -1;
        }
        std::ignore = gitRun({QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-C"), hash});
        m_currentCommitEmpty = true;
        return std::nullopt;
    }
    if (mode == GitRevLoglist::RebasePick) {
        int choice = -1;
        if (!m_autoSkipFailedCommit) {
            choice = askUser(QMessageBox::Question,
                             i18n("Cherry-pick failed (please see log in the cherry pick/rebase dialog for details)! Skip this commit?"),
                             {i18n("&Skip"), i18n("&Retry"), i18n("Cancel")},
                             i18n("&Do the same for the rest"),
                             &m_autoSkipFailedCommit);
            if (choice == 2) {
                m_autoSkipFailedCommit = false;
                retry = true;
                return 0;
            }
        }
        if ((m_autoSkipFailedCommit || choice == 1) && runGitCmdRetryOrAbort({QStringLiteral("reset"), QStringLiteral("--hard")})) {
            setRebaseAction(row, GitRevLoglist::RebaseSkip);
            return 0;
        }
        m_rebaseStage = RebaseStage::Error;
        addLogString(QStringLiteral("An unrecoverable error occurred."));
        return -1;
    }
    if (mode == GitRevLoglist::RebaseEdit) {
        m_rebaseStage = RebaseStage::Edit;
        return -1;
    }
    if (!isNextCommitSquash()) {
        // Let the user edit the last commit message
        m_rebaseStage = RebaseStage::SquashEdit;
        return -1;
    }
    return std::nullopt;
}

int RebaseDlg::handlePickSuccess(int row, const QString& hash, unsigned int mode, bool noCommit)
{
    // DoRebase after a successful pick
    if (mode == GitRevLoglist::RebasePick) {
        if (noCommit) {
            m_forRewrite.append(hash);
        } else {
            const QString head = hashOf(QStringLiteral("HEAD"));
            if (head.isEmpty()) {
                showError(i18n("Could not get HEAD hash."));
                m_rebaseStage = RebaseStage::Error;
                return -1;
            }
            m_rewrittenCommitsMap.insert(hash, head);
        }
        addRebaseAction(row, GitRevLoglist::RebaseDone);
        return 0;
    }
    if (mode == GitRevLoglist::RebaseEdit) {
        // Edit stops the rebase
        m_rebaseStage = RebaseStage::Edit;
        return -1;
    }
    if (!isNextCommitSquash()) {
        // Let the user edit the last commit message
        m_rebaseStage = RebaseStage::SquashEdit;
        return -1;
    }
    if (mode == GitRevLoglist::RebaseSquash) {
        addRebaseAction(row, GitRevLoglist::RebaseDone);
        m_forRewrite.append(hash);
    }
    return 0;
}

bool RebaseDlg::finishRebase()
{
    // FinishRebase
    if (m_finishedRebase) {
        return true;
    }
    m_finishedRebase = true;
    rewriteNotes();
    const QString head = hashOf(QStringLiteral("HEAD"));
    if (head.isEmpty()) {
        showError(i18n("Could not get HEAD hash."));
        return false;
    }
    setActiveTab(LogTab);
    if (isLocalBranch(m_branch)) {
        const QStringList checkout{QStringLiteral("checkout"), QStringLiteral("-f"), QStringLiteral("-B"), m_branch, head, QStringLiteral("--")};
        addLogString(commandLine(checkout));
        if (!runGitCmdRetryOrAbort(checkout)) {
            return false;
        }
    }
    const QStringList reset{QStringLiteral("reset"), QStringLiteral("--hard"), head, QStringLiteral("--")};
    addLogString(commandLine(reset));
    if (!runGitCmdRetryOrAbort(reset)) {
        return false;
    }
    runOnUi([this] {
        while (m_tabs->count() > 1) {
            m_tabs->removeTab(0);
        }
        m_statusText = i18n("Finished rebasing.");
        setStatusText(m_statusText, false);
    });
    return true;
}

void RebaseDlg::rewriteNotes()
{
    // RewriteNotes: git notes copy --for-rewrite reads "old new" pairs from stdin
    QString rewrites;
    for (auto it = m_rewrittenCommitsMap.cbegin(); it != m_rewrittenCommitsMap.cend(); ++it) {
        if (!it.value().isEmpty()) {
            rewrites += it.key() + QLatin1Char(' ') + it.value() + QLatin1Char('\n');
        }
    }
    if (rewrites.isEmpty()) {
        return;
    }
    std::ignore = gitRun({QStringLiteral("notes"), QStringLiteral("copy"), QStringLiteral("--for-rewrite=rebase")}, rewrites.toUtf8());
}

bool RebaseDlg::verifyNoConflict()
{
    // VerifyNoConflict
    bool hasConflicts = false;
    try {
        hasConflicts = m_git.hasWorkingTreeConflicts();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        addLogString(QStringLiteral("Checking for conflicts failed.\n") + e.error().message);
        return false;
    }
    if (hasConflicts) {
        std::ignore = askUser(QMessageBox::Warning, i18n("One or more files are in a conflicted state."), {i18n("OK")});
        runOnUi([this] {
            selectFirstConflict();
        });
        return false;
    }
    runOnUi([this] {
        cleanUpRebaseActiveFolder();
    });
    return true;
}

void RebaseDlg::resetParentForSquash(const QString& commitMessage)
{
    // ResetParentForSquash: so the next git cherry-pick --no-commit adds no unwanted commit
    m_squashMessage = commitMessage;
    setActiveTab(LogTab);
    std::ignore = runGitCmdRetryOrAbort({QStringLiteral("reset"), QStringLiteral("--soft"), QStringLiteral("HEAD~1"), QStringLiteral("--")});
}

bool RebaseDlg::isNextCommitSquash()
{
    // CheckNextCommitIsSquash() == 0: the next commit not skipped is a squash. The list is newest first.
    bool isSquash = false;
    runOnUi([this, &isSquash] {
        const GitLogListModel *model = m_commitList->logModel();
        for (int index = m_currentRebaseIndex - 1; index >= 0 && index < model->rowCount(); --index) {
            const unsigned int action = model->rev(index).rebaseAction;
            if (action & GitRevLoglist::RebaseSquash) {
                isSquash = true;
                return;
            }
            if (!(action & GitRevLoglist::RebaseSkip)) {
                return;
            }
        }
    });
    return isSquash;
}

void RebaseDlg::goNext()
{
    // GoNext, with m_IsOldFirst off
    --m_currentRebaseIndex;
}

bool RebaseDlg::isEnd() const
{
    // IsEnd
    return m_currentRebaseIndex < 0;
}

int RebaseDlg::currentCommitId()
{
    // GetCurrentCommitID
    return rowCount() - m_currentRebaseIndex;
}

int RebaseDlg::isCommitEmpty(const QString& hash)
{
    // IsCommitEmpty: whether the commit has its parent's tree
    const QStringList treeCommand{QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"), hash + QStringLiteral("^{tree}")};
    const GitResult tree = gitRun(treeCommand);
    if (!tree.ok) {
        addLogString(commandLine(treeCommand));
        addLogString(tree.output);
        return -1;
    }
    const GitResult parentTree = gitRun({QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"), hash + QStringLiteral("^^{tree}")});
    const QString parent = parentTree.ok ? parentTree.output.trimmed() : QLatin1String(emptyTreeHash);
    return tree.output.trimmed() == parent ? 1 : 0;
}

bool RebaseDlg::runGitCmdRetryOrAbort(const QStringList& arguments)
{
    // RunGitCmdRetryOrAbort
    while (true) {
        const GitResult result = gitRun(arguments);
        if (result.ok) {
            return true;
        }
        addLogString(commandLine(arguments));
        addLogString(i18n("Fail"));
        addLogString(result.output);
        const QString message = QStringLiteral("\"%1\" failed.\n%2").arg(commandLine(arguments), result.output);
        if (askUser(QMessageBox::Critical, message, {i18n("&Retry"), i18n("&Abort")}) != 1) {
            return false;
        }
    }
}

void RebaseDlg::abortJob()
{
    // OnBnClickedAbort after the question: put the branch and HEAD back
    setActiveTab(LogTab);
    if (m_isFastForward) {
        std::ignore = runGitCmdRetryOrAbort({QStringLiteral("reset"), QStringLiteral("--hard"), m_origBranchHash, QStringLiteral("--")});
        return;
    }
    if (m_origHeadBranch == m_branch) {
        const QStringList checkout = isLocalBranch(m_origHeadBranch)
            ? QStringList{QStringLiteral("checkout"), QStringLiteral("-f"), QStringLiteral("-B"), m_branch, m_origBranchHash, QStringLiteral("--")}
            : QStringList{QStringLiteral("checkout"), QStringLiteral("-f"), m_origBranchHash, QStringLiteral("--")};
        const GitResult result = gitRun(checkout);
        if (!result.ok) {
            addLogString(result.output);
            showError(QStringLiteral("Unrecoverable error on cleanup:\n") + result.output);
            return;
        }
        std::ignore = runGitCmdRetryOrAbort({QStringLiteral("reset"), QStringLiteral("--hard"), m_origBranchHash, QStringLiteral("--")});
        return;
    }
    restoreAfterAbort();
}

void RebaseDlg::restoreAfterAbort()
{
    // OnBnClickedAbort when another branch than the one checked out was rebased
    QString current = m_git.currentBranch();
    if (current == QLatin1String("(no branch)")) {
        current = QStringLiteral("HEAD");
    }
    if (m_origHeadBranch != current) {
        const QStringList checkout = isLocalBranch(m_origHeadBranch)
            ? QStringList{QStringLiteral("checkout"), QStringLiteral("-f"), QStringLiteral("-B"), m_origHeadBranch, m_origHeadHash, QStringLiteral("--")}
            : QStringList{QStringLiteral("checkout"), QStringLiteral("-f"), m_origHeadHash, QStringLiteral("--")};
        const GitResult result = gitRun(checkout);
        if (!result.ok) {
            // Go on to restore the moved branch
            addLogString(result.output);
            showError(QStringLiteral("Unrecoverable error on cleanup:\n") + result.output);
        }
    }
    std::ignore = runGitCmdRetryOrAbort({QStringLiteral("reset"), QStringLiteral("--hard"), m_origHeadHash, QStringLiteral("--")});
    // Restore the moved branch
    if (isLocalBranch(m_branch)) {
        const GitResult result = gitRun({QStringLiteral("branch"), QStringLiteral("-f"), QStringLiteral("--"), m_branch, m_origBranchHash});
        if (!result.ok) {
            addLogString(result.output);
            showError(QStringLiteral("Unrecoverable error on cleanup:\n") + result.output);
        }
    }
}

RebaseDlg::GitResult RebaseDlg::gitRun(const QStringList& arguments, const QByteArray& input) const
{
    // CGit::Run: stdout on success, the error with git's stderr on failure
    try {
        return {.ok = true, .output = QString::fromUtf8(m_git.run(arguments, RunMode::ReadWrite, input))};
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return {.ok = false, .output = e.error().message};
    }
}

QString RebaseDlg::hashOf(const QString& ref) const
{
    // CGit::GetHash, peeled to the commit
    if (isFullHash(ref)) {
        return ref;
    }
    if (ref.isEmpty()) {
        return {};
    }
    const GitResult result = gitRun(
        {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--end-of-options"), m_git.fixBranchName(ref) + QStringLiteral("^{commit}")});
    return result.ok ? result.output.trimmed() : QString();
}

std::optional<GitRevLoglist> RebaseDlg::getCommit(const QString& ref) const
{
    // GitRev::GetCommit
    QStringList arguments = GitRevLoglist::logArguments({}, m_git.fixBranchName(ref));
    arguments.insert(1, QStringLiteral("-1"));
    const GitResult result = gitRun(arguments);
    if (!result.ok) {
        return std::nullopt;
    }
    QByteArray output = result.output.toUtf8();
    const Result<QList<GitRevLoglist>> revs = GitRevLoglist::takeRecords(output);
    if (!revs || revs->isEmpty()) {
        return std::nullopt;
    }
    return revs->constFirst();
}

bool RebaseDlg::isFastForward(const QString& fromRef, const QString& toRef, QString *commonAncestor) const
{
    // CGit::IsFastForward: whether "from" is an ancestor of "to"
    const QString fromHash = hashOf(fromRef);
    const GitResult base = gitRun({QStringLiteral("merge-base"), QStringLiteral("--"), m_git.fixBranchName(toRef), m_git.fixBranchName(fromRef)});
    if (!base.ok || fromHash.isEmpty()) {
        return false;
    }
    const QString baseHash = base.output.trimmed();
    if (commonAncestor) {
        *commonAncestor = baseHash;
    }
    return fromHash == baseHash;
}

bool RebaseDlg::checkCleanWorkTree(bool stagedOk) const
{
    // CGit::CheckCleanWorkTree (the git.exe branch)
    if (!gitRun({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("HEAD")}).ok) {
        return false;
    }
    if (!gitRun({QStringLiteral("update-index"), QStringLiteral("--ignore-submodules"), QStringLiteral("--refresh")}).ok) {
        return false;
    }
    if (!gitRun({QStringLiteral("diff-files"), QStringLiteral("--quiet"), QStringLiteral("--ignore-submodules")}).ok) {
        return false;
    }
    return stagedOk
        || gitRun({QStringLiteral("diff-index"),
                   QStringLiteral("--cached"),
                   QStringLiteral("--quiet"),
                   QStringLiteral("HEAD"),
                   QStringLiteral("--ignore-submodules"),
                   QStringLiteral("--")})
               .ok;
}

bool RebaseDlg::isLocalBranch(const QString& name) const
{
    // CGit::IsLocalBranch
    return m_git.branchTagExists(name, true);
}

GitRevLoglist RebaseDlg::currentRev()
{
    return revAt(m_currentRebaseIndex);
}

GitRevLoglist RebaseDlg::revAt(int row)
{
    // A copy taken on the UI thread, which alone changes the model (the log list adds files as it draws)
    GitRevLoglist rev;
    runOnUi([this, row, &rev] {
        rev = m_commitList->logModel()->rev(row);
    });
    return rev;
}

int RebaseDlg::rowCount()
{
    int count = 0;
    runOnUi([this, &count] {
        count = m_commitList->logModel()->rowCount();
    });
    return count;
}

void RebaseDlg::setRebaseAction(int row, unsigned int action)
{
    runOnUi([this, row, action] {
        m_commitList->logModel()->setRebaseAction(row, action);
    });
}

void RebaseDlg::addRebaseAction(int row, unsigned int flags)
{
    runOnUi([this, row, flags] {
        GitLogListModel *model = m_commitList->logModel();
        model->setRebaseAction(row, model->rev(row).rebaseAction | flags);
    });
}

void RebaseDlg::addLogString(const QString& text)
{
    // AddLogString
    runOnUi([this, text] {
        m_outputRebase->appendPlainText(text);
    });
}

void RebaseDlg::setActiveTab(int tab)
{
    runOnUi([this, tab] {
        if (tab < m_tabs->count()) {
            m_tabs->setCurrentIndex(tab);
        }
    });
}

int RebaseDlg::askUser(int icon, const QString& text, const QStringList& buttons, const QString& checkText, bool *checked)
{
    // CMessageBox::Show and ShowCheck: the 1-based number of the button pressed, 0 if none was
    int answer = 0;
    runOnUi([&] {
        QMessageBox box(static_cast<QMessageBox::Icon>(icon), i18n("KortoiseGit"), text, QMessageBox::NoButton, this);
        QList<QAbstractButton *> added;
        for (const QString& button : buttons) {
            added.append(box.addButton(button, QMessageBox::ActionRole));
        }
        box.setDefaultButton(qobject_cast<QPushButton *>(added.constFirst()));
        box.setEscapeButton(added.constLast());
        QCheckBox *checkBox = nullptr;
        if (!checkText.isEmpty()) {
            checkBox = new QCheckBox(checkText, &box);
            box.setCheckBox(checkBox);
        }
        box.exec();
        answer = static_cast<int>(added.indexOf(box.clickedButton())) + 1;
        if (checkBox && checked) {
            *checked = checkBox->isChecked();
        }
    });
    return answer;
}

void RebaseDlg::showError(const QString& text)
{
    std::ignore = askUser(QMessageBox::Critical, text, {i18n("OK")});
}

void RebaseDlg::runOnUi(const std::function<void()>& job)
{
    // TortoiseGit's SendMessage from the rebase thread: run on the UI thread and wait for it
    if (QThread::currentThread() == thread()) {
        job();
        return;
    }
    QMetaObject::invokeMethod(this, job, Qt::BlockingQueuedConnection);
}

} // namespace kgt
