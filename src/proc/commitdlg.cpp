// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CommitDlg.cpp: OnInitDialog (the layout), StatusThread (what the list
 * shows and checks), OnUpdateOKButton, OnBnClickedShowunversioned, OnCheck (All and None), and OnOK with
 * PrepareIndexForCommitWithoutStagingSupport. TortoiseGit's progress dialog for git commit becomes an error message
 * inside the dialog.
 */

#include "commitdlg.hpp"

#include "git/git.hpp"
#include "git/gitadmindir.hpp"
#include "git/gitcall.hpp"
#include "gitstatuslistctrl.hpp"
#include "gitstatuslistmodel.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>
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
#include <QSplitter>
#include <QTemporaryFile>
#include <QVBoxLayout>

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
 * @param path A path relative to the working tree root.
 * @returns It as a pathspec that matches only that path, whatever characters it holds.
 */
QString literalPathSpec(const QString& path)
{
    return QStringLiteral(":(literal)") + path;
}

/**
 * Adds one command for a list of paths, as CMassiveGitTask runs one, if the list isn't empty.
 * @param[in,out] steps Where to add it.
 * @param step The command: its arguments before the paths, and how it runs.
 * @param paths The paths, relative to the working tree root.
 */
void addTask(QList<CommitStep>& steps, CommitStep step, const QStringList& paths)
{
    if (paths.isEmpty()) {
        return;
    }
    // update-index takes plain paths; the others take pathspecs, so make those literal
    const bool takesPathSpecs = step.arguments.constFirst() != QLatin1String("update-index");
    step.arguments.append(QStringLiteral("--"));
    for (const QString& path : paths) {
        step.arguments.append(takesPathSpecs ? literalPathSpec(path) : path);
    }
    steps.append(std::move(step));
}

/**
 * @param command The git arguments before the paths, separated by spaces.
 * @param ignoreErrors Whether a failure doesn't stop the commit.
 * @param always Whether it runs even after a failure.
 * @returns The step.
 */
CommitStep task(QLatin1StringView command, bool ignoreErrors = false, bool always = false)
{
    return CommitStep{.arguments = QString(command).split(QLatin1Char(' ')), .ignoreErrors = ignoreErrors, .always = always};
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

    m_commitTo->setText(currentBranch(m_workingTree));
    m_fileList->setWorkingTree(m_workingTree);
    m_fileList->setPathFilter(relativePaths(m_workingTree, paths));
    m_fileList->setIncludeStaged(true);
    onShowUnversionedToggled(m_showUnversioned->isChecked());
    m_fileList->reload();
    m_message->setFocus();
}

CommitDlg::~CommitDlg() = default;

CommitPlan CommitDlg::planCommit(const GitStatusListModel& model)
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
    addTask(plan.prepareIndex, task("reset --quiet"_L1, true), tasks.reset);

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

bool CommitDlg::isCommitting() const
{
    return m_isCommitting;
}

void CommitDlg::commit()
{
    if (!m_okButton->isEnabled()) {
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

    const CommitPlan plan = planCommit(*m_fileList->statusModel());
    m_steps = plan.prepareIndex;
    m_steps.append(
        CommitStep{.arguments = {QStringLiteral("commit"), QStringLiteral("-F"), m_messageFile->fileName()}, .ignoreErrors = false, .always = false});
    m_steps.append(plan.restoreIndex);
    setBusy(true);
    runNextStep();
}

void CommitDlg::reject()
{
    if (m_isCommitting) {
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
    connect(m_fileList, &GitStatusListCtrl::loaded, this, &CommitDlg::updateOkButton);
    connect(m_fileList, &GitStatusListCtrl::loadFailed, this, &CommitDlg::updateOkButton);
    connect(m_showUnversioned, &QCheckBox::toggled, this, &CommitDlg::onShowUnversionedToggled);

    updateOkButton();
    resize(720, 600);
}

void CommitDlg::updateOkButton()
{
    // OnUpdateOKButton: a message that isn't only whitespace, and at least one file checked
    const bool hasMessage = !m_message->toPlainText().trimmed().isEmpty();
    const bool hasFiles = !m_fileList->isBusy() && m_fileList->statusModel()->checkedCount() > 0;
    m_okButton->setEnabled(!m_isCommitting && hasMessage && hasFiles);
}

void CommitDlg::onShowUnversionedToggled(bool checked)
{
    const GitStatusListModel::ShowFlags show = showFlags(checked);
    m_fileList->setShowFlags(show, checkFlags(show));
}

void CommitDlg::runNextStep()
{
    // After a failure only the steps that put the index back still run
    if (m_commitError) {
        m_steps.removeIf([](const CommitStep& step) {
            return !step.always;
        });
    }
    if (m_steps.isEmpty()) {
        finishCommit();
        return;
    }
    m_runningStep = m_steps.takeFirst();
    m_stepCall = Git(m_workingTree).runAsync(m_runningStep.arguments);
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
        accept();
        return;
    }
    // TortoiseGit leaves the dialog open, with the message, after git commit fails
    m_errorView->setText(m_commitError->message);
    m_errorView->show();
    m_fileList->reload();
    Q_EMIT commitFailed(*m_commitError);
}

void CommitDlg::setBusy(bool busy)
{
    m_isCommitting = busy;
    m_message->setReadOnly(busy);
    m_fileList->setEnabled(!busy);
    m_showUnversioned->setEnabled(!busy);
    if (busy) {
        setCursor(Qt::BusyCursor);
    } else {
        unsetCursor();
    }
    updateOkButton();
}

} // namespace kgt
