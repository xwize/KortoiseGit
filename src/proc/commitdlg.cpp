// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CommitDlg.cpp: OnInitDialog (the layout), StatusThread (what the list
 * shows and checks), OnUpdateOKButton, OnBnClickedShowunversioned, OnBnClickedCommitAmend, OnCheck (All and None),
 * and OnOK with PrepareIndexForCommitWithoutStagingSupport. TortoiseGit's progress dialog for git commit becomes an error message
 * inside the dialog.
 */

#include "commitdlg.hpp"

#include "git/git.hpp"
#include "git/gitadmindir.hpp"
#include "git/gitcall.hpp"
#include "git/massivegittask.hpp"
#include "gitstatuslistctrl.hpp"
#include "gitstatuslistmodel.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KMessageWidget>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTemporaryFile>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>
#include <utility>

namespace
{

using namespace kgt;
using namespace Qt::StringLiterals;
using Action = TGitPath::Action;
using Show = GitStatusListModel::Show;

// TortoiseGit's ReaddUnselectedAddedFilesAfterCommit, on by default; not configurable yet
constexpr bool readdUnselectedAddedFilesAfterCommit = true;

/**
 * The commands of one commit, gathered per kind as TortoiseGit's CMassiveGitTask objects are.
 */
struct IndexTasks {
    QStringList add; // add -f
    QStringList updateIndexForceRemove; // update-index --force-remove
    QStringList updateIndex; // update-index
    QStringList rm; // rm --ignore-unmatch
    QStringList rmFCache; // rm -f --cached
    QStringList reset; // reset
    QStringList reAddAfterCommit; // add --ignore-errors -f
    QStringList reDelAfterCommit; // rm --cached --ignore-unmatch
};

/**
 * Adds one command for a list of paths, as CMassiveGitTask runs one, if the list isn't empty. The paths go in on stdin,
 * so that no list is too long for a command line, and are taken literally, whatever characters they hold.
 * @param[in,out] steps Where to add it.
 * @param step The command: its arguments before the paths, and how it runs.
 * @param paths The paths, relative to the working tree root.
 */
void addTask(QList<CommitStep>& steps, CommitStep step, const QStringList& paths)
{
    if (paths.isEmpty()) {
        return;
    }
    if (step.arguments.constFirst() == QLatin1String("update-index")) {
        // Takes plain paths, and no --pathspec-from-file
        step.arguments << QStringLiteral("-z") << QStringLiteral("--stdin");
        for (const QString& path : paths) {
            step.input.append(path.toUtf8());
            step.input.append('\0');
        }
    } else {
        MassiveGitTask massiveTask(step.arguments);
        massiveTask.setPaths(paths);
        step.arguments = massiveTask.arguments();
        step.input = massiveTask.pathSpecFile();
    }
    steps.append(std::move(step));
}

/**
 * @param command The git arguments before the paths, separated by spaces.
 * @param ignoreErrors Whether a failure doesn't stop the commit.
 * @param afterCommit Whether it follows git commit, and runs even if git commit failed.
 * @returns The step.
 */
CommitStep task(QLatin1StringView command, bool ignoreErrors = false, bool afterCommit = false)
{
    return CommitStep{.arguments = QString(command).split(QLatin1Char(' ')), .input = {}, .ignoreErrors = ignoreErrors, .afterCommit = afterCommit};
}

/**
 * A checked file: it goes into the commit.
 * @param entry The file.
 * @param[in,out] tasks Where its commands are added.
 */
void planChecked(const GitStatusListEntry& entry, IndexTasks& tasks)
{
    if (entry.actions & Action::Unversioned) {
        tasks.add.append(entry.path);
    } else if (entry.actions & Action::Deleted) {
        tasks.updateIndexForceRemove.append(entry.path);
    } else {
        tasks.updateIndex.append(entry.path);
    }
    if ((entry.actions & Action::Replaced) && !entry.origPath.isEmpty()) {
        tasks.rm.append(entry.origPath);
    }
}

/**
 * An unchecked file: it is left out of the commit, and staged again afterwards if it was added.
 * @param entry The file.
 * @param[in,out] tasks Where its commands are added.
 */
void planUnchecked(const GitStatusListEntry& entry, IndexTasks& tasks)
{
    if (entry.actions & (Action::Added | Action::Replaced)) {
        tasks.rmFCache.append(entry.path);
        tasks.reAddAfterCommit.append(entry.path);
        if ((entry.actions & Action::Replaced) && !entry.origPath.isEmpty()) {
            tasks.reset.append(entry.origPath);
            tasks.reDelAfterCommit.append(entry.origPath);
        }
    } else if (!(entry.actions & Action::Unversioned)) {
        tasks.reset.append(entry.path);
        if ((entry.actions & Action::Deleted) && !(entry.actions & Action::Missing)) {
            tasks.reDelAfterCommit.append(entry.path);
        }
    }
}

/**
 * @param path A path given to the dialog.
 * @returns It with symbolic links in its directory resolved, as git reports the working tree root.
 */
QString resolvedPath(const QString& path)
{
    const QFileInfo info(path);
    const QString dir = QFileInfo(info.absolutePath()).canonicalFilePath();
    return dir.isEmpty() ? info.absoluteFilePath() : QDir(dir).filePath(info.fileName());
}

/**
 * @param root The working tree root.
 * @param paths The paths given to the dialog.
 * @returns The paths relative to the root, or an empty list when one of them is the root (the whole tree).
 */
QStringList relativePaths(const QString& root, const QStringList& paths)
{
    QStringList relative;
    for (const QString& path : paths) {
        QString rel = QDir(root).relativeFilePath(resolvedPath(path));
        if (rel.isEmpty() || rel == QLatin1String(".")) {
            return {};
        }
        if (rel.endsWith(QLatin1Char('/'))) {
            rel.chop(1);
        }
        relative.append(rel);
    }
    return relative;
}

/**
 * @param root The working tree root.
 * @returns The current branch, as CGit::GetCurrentBranch shows it: "(no branch)" when HEAD is detached.
 */
QString currentBranch(const QString& root)
{
    const QString branch = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(root));
    if (branch.isEmpty() || branch == QLatin1String("HEAD")) {
        return i18n("(no branch)");
    }
    return branch;
}

/**
 * @param showUnversioned Whether unversioned files are listed.
 * @returns The list's show flags (StatusThread's dwShow): every change, and unversioned files if asked.
 */
GitStatusListModel::ShowFlags showFlags(bool showUnversioned)
{
    GitStatusListModel::ShowFlags show = Show::VersionedButNormal;
    if (showUnversioned) {
        show |= Show::Unversioned;
    }
    return show;
}

/**
 * @param show The list's show flags.
 * @returns Which entries start checked: everything listed but unversioned files (SelectFilesForCommit is on).
 */
GitStatusListModel::ShowFlags checkFlags(GitStatusListModel::ShowFlags show)
{
    return show & ~GitStatusListModel::ShowFlags(Show::Unversioned);
}

/**
 * @param root The working tree root.
 * @param name A file in the working tree's admin directory, such as "MERGE_HEAD".
 * @returns Whether it exists.
 */
bool adminFileExists(const QString& root, const QString& name)
{
    return QFileInfo::exists(QDir(GitAdminDir::worktreeAdminDirPath(root)).filePath(name));
}

/**
 * @param root The working tree root.
 * @returns Whether HEAD has no parent, as a root commit (GitRev::ParentsCount() < 1).
 */
bool isHeadRootCommit(const QString& root)
{
    try {
        const QByteArray line =
            Git(root).run({QStringLiteral("rev-list"), QStringLiteral("--parents"), QStringLiteral("-n"), QStringLiteral("1"), QStringLiteral("HEAD")},
                          RunMode::ReadOnly);
        return line.trimmed().split(' ').size() < 2;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return false;
    }
}

} // namespace

namespace kgt
{

CommitDlg::CommitDlg(const QStringList& paths, QWidget *parent)
    : QDialog(parent)
{
    Q_ASSERT(!paths.isEmpty());
    m_workingTree = Git::workingTreeRoot(paths.constFirst());
    createWidgets();

    // SetDlgTitle: the one path the dialog was opened for, or the working tree for several
    const QString titlePath = paths.size() == 1 ? QDir::toNativeSeparators(paths.constFirst()) : m_workingTree;
    setWindowTitle(i18nc("@title:window %1 is a path", "%1 - Commit", titlePath));

    m_fileList->setWorkingTree(m_workingTree);
    m_fileList->setPathFilter(relativePaths(m_workingTree, paths));
    m_fileList->setIncludeStaged(true);
    onShowUnversionedToggled(m_showUnversioned->isChecked());
    refresh();
    m_message->setFocus();
}

CommitDlg::~CommitDlg() = default;

CommitPlan CommitDlg::planCommit(const GitStatusListModel& model, bool amendAgainstParent)
{
    CommitPlan plan;
    IndexTasks tasks;
    for (int row = 0; row < model.rowCount(); ++row) {
        const bool checked = model.data(model.index(row, GitStatusListModel::PathColumn), Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked;
        if (checked) {
            planChecked(model.entry(row), tasks);
            ++plan.checkedCount;
        } else {
            planUnchecked(model.entry(row), tasks);
        }
    }

    // In TortoiseGit's order; resetting may fail in a repository without commits, and that is fine
    addTask(plan.prepareIndex, task("add -f"_L1), tasks.add);
    addTask(plan.prepareIndex, task("update-index --force-remove"_L1), tasks.updateIndexForceRemove);
    addTask(plan.prepareIndex, task("update-index"_L1), tasks.updateIndex);
    addTask(plan.prepareIndex, task("rm --ignore-unmatch --quiet"_L1), tasks.rm);
    addTask(plan.prepareIndex, task("rm -f --cached --quiet"_L1), tasks.rmFCache);
    addTask(plan.prepareIndex, task(amendAgainstParent ? "reset --quiet HEAD~1"_L1 : "reset --quiet"_L1, true), tasks.reset);

    if (readdUnselectedAddedFilesAfterCommit) {
        addTask(plan.restoreIndex, task("add --ignore-errors -f"_L1, false, true), tasks.reAddAfterCommit);
        addTask(plan.restoreIndex, task("rm --cached --ignore-unmatch --quiet"_L1, false, true), tasks.reDelAfterCommit);
    }
    return plan;
}

GitStatusListCtrl *CommitDlg::fileList() const
{
    return m_fileList;
}

QPlainTextEdit *CommitDlg::messageEdit() const
{
    return m_message;
}

QPushButton *CommitDlg::commitButton() const
{
    return m_okButton;
}

QCheckBox *CommitDlg::showUnversionedButton() const
{
    return m_showUnversioned;
}

QCheckBox *CommitDlg::amendButton() const
{
    return m_amendButton;
}

bool CommitDlg::isCommitting() const
{
    return m_isCommitting;
}

void CommitDlg::commit()
{
    if (!m_okButton->isEnabled() || !confirmNoConflicts()) {
        return;
    }
    m_errorView->hide();
    m_commitError.reset();

    // SaveCommitUnicodeFile: the message goes to git in a file, never on the command line
    m_messageFile = std::make_unique<QTemporaryFile>();
    if (!m_messageFile->open() || m_messageFile->write(m_message->toPlainText().toUtf8()) < 0 || !m_messageFile->flush()) {
        m_errorView->setText(i18n("Could not save commit message"));
        m_errorView->show();
        Q_EMIT commitFailed(Error{.category = ErrorCategory::Io, .message = m_errorView->text()});
        return;
    }

    const CommitPlan plan = planCommit(*m_fileList->statusModel(), isAmending() && !m_amendDiffToLastCommit);
    m_steps = plan.prepareIndex;
    QStringList commitArguments{QStringLiteral("commit")};
    if (isAmending()) {
        commitArguments.append(QStringLiteral("--amend"));
    }
    commitArguments.append({QStringLiteral("-F"), m_messageFile->fileName()});
    m_steps.append(CommitStep{.arguments = commitArguments, .input = {}, .ignoreErrors = false, .afterCommit = false});
    m_steps.append(plan.restoreIndex);
    setBusy(true);
    runNextStep();
}

bool CommitDlg::confirmNoConflicts()
{
    // CCommitDlg::OnOK: conflict markers must not be committed by accident
    if (!m_fileList->hasConflicts()) {
        return true;
    }
    const auto answer = KMessageBox::warningTwoActions(this,
                                                       i18n("One or more files are in a conflicted state."),
                                                       QString(),
                                                       KGuiItem(i18n("OK")),
                                                       KGuiItem(i18n("Ignore")),
                                                       QStringLiteral("CommitWarnOnUnresolved"),
                                                       KMessageBox::Options());
    if (answer == KMessageBox::PrimaryAction) {
        m_fileList->selectFirstConflict();
        return false;
    }
    return true;
}

void CommitDlg::reject()
{
    // Closing kills whatever is running, and a killed git can leave index.lock behind
    if (m_isCommitting || m_fileList->isModifying()) {
        return;
    }
    QDialog::reject();
}

void CommitDlg::createWidgets()
{
    auto *layout = new QVBoxLayout(this);

    auto *commitToRow = new QHBoxLayout;
    commitToRow->addWidget(new QLabel(i18n("Commit to:"), this));
    m_commitTo = new QLineEdit(this);
    m_commitTo->setReadOnly(true);
    m_commitTo->setFrame(false);
    commitToRow->addWidget(m_commitTo, 1);
    layout->addLayout(commitToRow);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    auto *messageGroup = new QGroupBox(i18n("&Message:"), splitter);
    auto *messageLayout = new QVBoxLayout(messageGroup);
    m_message = new QPlainTextEdit(messageGroup);
    // TortoiseGit's message editor uses a fixed-width font, so the 50/72 conventions can be seen
    m_message->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_message->setTabChangesFocus(true);
    messageLayout->addWidget(m_message);
    m_amendButton = new QCheckBox(i18n("Amend &Last Commit"), messageGroup);
    m_amendButton->setToolTip(
        i18n("Use this to modify/replace the latest checked out commit with a new commit. The message from the original commit is used as the "
             "starting point. Attention: This modifies the Git history."));
    m_amendButton->setEnabled(false);
    messageLayout->addWidget(m_amendButton);
    splitter->addWidget(messageGroup);

    auto *listGroup = new QGroupBox(i18n("Changes made (F5: refresh, double-click on file for diff):"), splitter);
    auto *listLayout = new QVBoxLayout(listGroup);
    auto *checkRow = new QHBoxLayout;
    checkRow->addWidget(new QLabel(i18n("Check:"), listGroup));
    for (const auto& [text, check] : {std::pair{i18n("All"), true}, std::pair{i18n("None"), false}}) {
        auto *link = new QLabel(QStringLiteral("<a href=\"#\">%1</a>").arg(text.toHtmlEscaped()), listGroup);
        connect(link, &QLabel::linkActivated, this, [this, check] {
            m_fileList->statusModel()->setAllChecked(check);
        });
        checkRow->addWidget(link);
    }
    checkRow->addStretch();
    listLayout->addLayout(checkRow);

    m_fileList = new GitStatusListCtrl(listGroup);
    m_fileList->setEmptyString(i18n("No files were changed or added since\nthe last commit."));
    listLayout->addWidget(m_fileList);

    auto *bottomRow = new QHBoxLayout;
    m_showUnversioned = new QCheckBox(i18n("Show &Unversioned Files"), listGroup);
    // TortoiseGit's AddBeforeCommit, on by default; not remembered yet
    m_showUnversioned->setChecked(true);
    bottomRow->addWidget(m_showUnversioned);
    bottomRow->addStretch();
    m_statistics = new QLabel(listGroup);
    bottomRow->addWidget(m_statistics);
    listLayout->addLayout(bottomRow);
    m_fileList->setStatLabel(m_statistics);
    splitter->addWidget(listGroup);
    splitter->setStretchFactor(1, 1);

    m_errorView = new KMessageWidget(this);
    m_errorView->setMessageType(KMessageWidget::Error);
    m_errorView->setWordWrap(true);
    m_errorView->setCloseButtonVisible(true);
    m_errorView->hide();
    layout->addWidget(m_errorView);

    auto *buttons = new QDialogButtonBox(this);
    m_okButton = buttons->addButton(i18n("C&ommit"), QDialogButtonBox::AcceptRole);
    m_okButton->setDefault(false);
    m_okButton->setAutoDefault(false);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &CommitDlg::commit);
    connect(buttons, &QDialogButtonBox::rejected, this, &CommitDlg::reject);
    connect(m_message, &QPlainTextEdit::textChanged, this, &CommitDlg::updateOkButton);
    connect(m_fileList, &GitStatusListCtrl::checkedCountChanged, this, &CommitDlg::updateOkButton);
    connect(m_fileList, &GitStatusListCtrl::loaded, this, &CommitDlg::onListLoaded);
    connect(m_fileList, &GitStatusListCtrl::loadFailed, this, &CommitDlg::updateOkButton);
    connect(m_showUnversioned, &QCheckBox::toggled, this, &CommitDlg::onShowUnversionedToggled);
    // Only the user's click swaps the message (ON_BN_CLICKED); the dialog unticks it without swapping
    connect(m_amendButton, &QCheckBox::clicked, this, &CommitDlg::onAmendClicked);
    // PreTranslateMessage's VK_F5, wherever the focus is; ignored while m_bBlock (a commit or a status load is running)
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, [this] {
        if (!m_isCommitting && !m_fileList->isBusy()) {
            refresh();
        }
    });

    updateOkButton();
    resize(720, 600);
}

void CommitDlg::refresh()
{
    // StatusThread: amend is unavailable while the list loads, and is dropped during a cherry-pick
    m_amendButton->setEnabled(false);
    if (isCherryPickActive()) {
        const QSignalBlocker blocker(m_amendButton);
        m_amendButton->setChecked(false);
    }
    m_fileList->setAmend(isAmending() && !m_amendDiffToLastCommit);
    m_fileList->reload();
}

void CommitDlg::onListLoaded()
{
    m_commitTo->setText(currentBranch(m_workingTree));
    // StatusThread, after the list is filled: amend needs a HEAD, and isn't offered during a cherry-pick
    if (m_fileList->isInitialRepo()) {
        const QSignalBlocker blocker(m_amendButton);
        m_amendButton->setChecked(false);
        m_amendButton->setEnabled(false);
    } else {
        m_amendButton->setEnabled(!m_isCommitting && !isCherryPickActive());
        // A root commit has nothing to compare with but itself
        m_amendDiffToLastCommit = isHeadRootCommit(m_workingTree);
    }
    m_mergeActive = adminFileExists(m_workingTree, QStringLiteral("MERGE_HEAD"));
    updateOkButton();
}

void CommitDlg::onAmendClicked(bool checked)
{
    // OnBnClickedCommitAmend: HEAD's message is the starting point, and what the user typed comes back on untick
    if (checked && m_amendStr.isEmpty()) {
        try {
            // GitRev's subject and body, joined by the newline between them: the whole message
            QString message = QString::fromUtf8(
                Git(m_workingTree)
                    .run({QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B"), QStringLiteral("HEAD")}, RunMode::ReadOnly));
            while (message.endsWith(QLatin1Char('\n'))) {
                message.chop(1);
            }
            m_amendStr = message;
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            m_errorView->setText(e.error().message);
            m_errorView->show();
        }
    }
    if (checked) {
        m_noAmendStr = m_message->toPlainText();
        m_message->setPlainText(m_amendStr);
    } else {
        m_amendStr = m_message->toPlainText();
        m_message->setPlainText(m_noAmendStr);
    }
    m_message->setFocus();
    refresh();
}

bool CommitDlg::isAmending() const
{
    return m_amendButton->isChecked();
}

bool CommitDlg::isCherryPickActive() const
{
    return adminFileExists(m_workingTree, QStringLiteral("CHERRY_PICK_HEAD"));
}

void CommitDlg::updateOkButton()
{
    // OnUpdateOKButton: a message that isn't only whitespace, and at least one file checked, or amending a root
    // commit (bAmendOrSelectFilesOrMerge), or concluding a merge
    const bool hasMessage = !m_message->toPlainText().trimmed().isEmpty();
    const bool amendOrSelectFilesOrMerge = m_fileList->statusModel()->checkedCount() > 0 || (isAmending() && m_amendDiffToLastCommit) || m_mergeActive;
    m_okButton->setEnabled(!m_isCommitting && !m_fileList->isBusy() && hasMessage && amendOrSelectFilesOrMerge);
}

void CommitDlg::onShowUnversionedToggled(bool checked)
{
    const GitStatusListModel::ShowFlags show = showFlags(checked);
    m_fileList->setShowFlags(show, checkFlags(show));
}

void CommitDlg::runNextStep()
{
    // CCommitDlg::OnOK: when preparing the index fails (bAddSuccess), neither git commit nor the steps after it run.
    // When git commit fails only the steps after it are left, and they run; nothing unstages the checked files.
    if (m_commitError && std::ranges::any_of(m_steps, [](const CommitStep& step) {
            return !step.afterCommit;
        })) {
        m_steps.clear();
    }
    if (m_steps.isEmpty()) {
        finishCommit();
        return;
    }
    m_runningStep = m_steps.takeFirst();
    m_stepCall = Git(m_workingTree).runAsync(m_runningStep.arguments, RunMode::ReadWrite, m_runningStep.input);
    connect(m_stepCall.get(), &GitCall::finished, this, &CommitDlg::onStepFinished);
}

void CommitDlg::onStepFinished()
{
    // The call may still be emitting: delete it from the event loop
    GitCall *call = m_stepCall.release();
    call->deleteLater();
    try {
        std::ignore = call->output();
    } catch (const RuntimeError& e) {
        if (!m_runningStep.ignoreErrors && !m_commitError) {
            m_commitError = e.error();
        }
    }
    runNextStep();
}

void CommitDlg::finishCommit()
{
    m_messageFile.reset();
    setBusy(false);
    if (!m_commitError) {
        // TortoiseGit resets amend after a successful commit, for when the dialog stays open
        m_amendStr.clear();
        {
            const QSignalBlocker blocker(m_amendButton);
            m_amendButton->setChecked(false);
        }
        accept();
        return;
    }
    // TortoiseGit leaves the dialog open, with the message, after git commit fails
    const Error error = *m_commitError;
    m_errorView->setText(error.message);
    m_errorView->show();
    refresh();
    Q_EMIT commitFailed(error);
}

void CommitDlg::setBusy(bool busy)
{
    m_isCommitting = busy;
    m_message->setReadOnly(busy);
    m_fileList->setEnabled(!busy);
    m_showUnversioned->setEnabled(!busy);
    if (busy) {
        m_amendButton->setEnabled(false);
    }
    if (busy) {
        setCursor(Qt::BusyCursor);
    } else {
        unsetCursor();
    }
    updateOkButton();
}

} // namespace kgt
