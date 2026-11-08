// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/RebaseDlg.cpp and its IDD_REBASE resource: the dialog's controls, the
 * commit list and the stages' look. The rebase itself is in rebasedlg_rebase.cpp.
 *
 * Not ported yet: cherry-pick mode, the Add button (picking commits in the log), "Edit/Split commit", the
 * post-rebase buttons other than Done, the pre-rebase hook, the reflog entries TortoiseGit writes through libgit2,
 * and the SquashDate setting (TortoiseGit's default, the first commit's date, is used).
 */

#include "rebasedlg.hpp"

#include "apputils.hpp"
#include "browserefsdlg.hpp"
#include "git/gitadmindir.hpp"
#include "git/gitrevloglist.hpp"
#include "gitloglist.hpp"
#include "gitloglistmodel.hpp"
#include "gitstatuslistctrl.hpp"

#include <KLocalizedString>

#include <QAbstractProxyModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace
{

using namespace kgt;
using Show = GitStatusListModel::Show;

constexpr int maxToolTipLength = 8000;

/**
 * The entries of the "Squash ALL" button (IDS_PROC_REBASE_SELECTALL_* and IDS_PROC_REBASE_UNSELECTED_*), in
 * TortoiseGit's order.
 */
enum SplitAllEntry : int { // NOLINT(cppcoreguidelines-use-enum-class): CMenuButton entry indexes
    PickAll,
    SquashAll,
    EditAll,
    SkipUnselected,
    SquashUnselected,
    EditUnselected,
};

/**
 * @returns The texts of the "Squash ALL" button's entries, in SplitAllEntry order.
 */
QStringList splitAllTexts()
{
    return {
        i18n("&Pick ALL"),
        i18n("&Squash ALL"),
        i18n("&Edit ALL"),
        i18n("S&kip unselected"),
        i18n("S&quash unselected"),
        i18n("E&dit unselected"),
    };
}

/**
 * Ported from CHistoryCombo::AddString: puts a text at the top of a combo box, once, and selects it.
 * @param combo The combo box.
 * @param text The text.
 */
void addString(QComboBox *combo, const QString& text)
{
    if (text.isEmpty()) {
        return;
    }
    const int existing = combo->findText(text, Qt::MatchExactly | Qt::MatchCaseSensitive);
    if (existing >= 0) {
        combo->removeItem(existing);
    }
    combo->insertItem(0, text);
    combo->setCurrentIndex(0);
}

} // namespace

namespace kgt
{

QString SquashFirstMetaData::author() const
{
    if (!set) {
        return {};
    }
    return QStringLiteral("%1 <%2>").arg(name, email);
}

QStringList SquashFirstMetaData::asParams(bool now) const
{
    if (!set) {
        return {};
    }
    const QString date = now ? QStringLiteral("now") : time.toString(Qt::ISODate);
    return {QStringLiteral("--date=") + date, QStringLiteral("--author=") + author()};
}

RebaseDlg::RebaseDlg(const QString& workingTree, const RebaseOptions& options, QWidget *parent)
    : QDialog(parent)
    , m_git(workingTree)
    , m_options(options)
    , m_branchCtrl(new QComboBox(this))
    , m_reverseButton(new QToolButton(this))
    , m_upstreamCtrl(new QComboBox(this))
    , m_browseButton(new QPushButton(QStringLiteral("..."), this))
    , m_ontoButton(new QPushButton(i18n("&Onto"), this))
    , m_splitter(new QSplitter(Qt::Vertical, this))
    , m_commitList(new GitLogList(this))
    , m_splitAllOptions(new QToolButton(this))
    , m_upButton(new QPushButton(i18n("&Up"), this))
    , m_downButton(new QPushButton(i18n("&Down"), this))
    , m_preserveMergesCheck(new QCheckBox(i18n("&Preserve merges"), this))
    , m_forceCheck(new QCheckBox(i18n("&Force Rebase"), this))
    , m_tabs(new QTabWidget(this))
    , m_fileListCtrl(new GitStatusListCtrl(this))
    , m_logMessageCtrl(new QPlainTextEdit(this))
    , m_outputRebase(new QPlainTextEdit(this))
    , m_progressBar(new QProgressBar(this))
    , m_statusLabel(new QLabel(this))
    , m_continueButton(new QPushButton(this))
    , m_abortButton(new QPushButton(i18n("Abort"), this))
{
    // CAppUtils::SetWindowTitle
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Rebase", QDir::toNativeSeparators(m_git.workingDirectory())));
    createWidgets();

    // OnInitDialog
    m_forceCheck->setChecked(m_options.force);
    m_commitList->setWorkingTree(m_git.workingDirectory());
    m_commitList->logModel()->setRebaseMode(true);
    // Room for the REBASE header and the longest action name
    const QFontMetrics metrics = m_commitList->header()->fontMetrics();
    int rebaseWidth = metrics.horizontalAdvance(i18n("REBASE"));
    for (const unsigned int action : {GitRevLoglist::RebasePick, GitRevLoglist::RebaseSquash, GitRevLoglist::RebaseEdit, GitRevLoglist::RebaseSkip}) {
        rebaseWidth = std::max(rebaseWidth, metrics.horizontalAdvance(GitLogListModel::rebaseActionName(action)));
    }
    m_commitList->header()->resizeSection(GitLogListModel::GraphColumn, rebaseWidth + (2 * metrics.averageCharWidth()));
    m_fileListCtrl->setWorkingTree(m_git.workingDirectory());
    // GITSLC_POPALL without commit, restore and changelists, which have no entries here
    m_fileListCtrl->setContextMenus(GitStatusListCtrl::ContextMenu::All);
    m_logMessageCtrl->setReadOnly(true);
    m_outputRebase->setReadOnly(true);
    m_outputRebase->setUndoRedoEnabled(false);
    setContinueButtonText();
    setControlEnable();
    m_continueButton->setEnabled(false);
    loadBranchInfo();

    resize(800, 600);
}

RebaseDlg::~RebaseDlg()
{
    if (m_thread) {
        /**
         * Only reached if the dialog is destroyed while the worker runs, which closing it never allows. The
         * worker may be waiting for the UI thread, so events are processed while it stops.
         */
        m_abortRequested = true;
        while (!m_thread->wait(10)) {
            QCoreApplication::processEvents();
        }
    }
}

void RebaseDlg::createWidgets()
{
    auto *layout = new QVBoxLayout(this);

    auto *refRow = new QHBoxLayout;
    auto *branchLabel = new QLabel(i18n("&Branch:"), this);
    branchLabel->setBuddy(m_branchCtrl);
    refRow->addWidget(branchLabel);
    refRow->addWidget(m_branchCtrl, 1);
    m_reverseButton->setIcon(QIcon::fromTheme(QStringLiteral("exchange-positions"), QIcon::fromTheme(QStringLiteral("object-flip-horizontal"))));
    refRow->addWidget(m_reverseButton);
    auto *upstreamLabel = new QLabel(i18n("&Upstream:"), this);
    upstreamLabel->setBuddy(m_upstreamCtrl);
    refRow->addWidget(upstreamLabel);
    refRow->addWidget(m_upstreamCtrl, 1);
    m_browseButton->setMaximumWidth(m_browseButton->fontMetrics().horizontalAdvance(QStringLiteral(" ... ")) * 2);
    refRow->addWidget(m_browseButton);
    m_ontoButton->setCheckable(true);
    refRow->addWidget(m_ontoButton);
    layout->addLayout(refRow);

    auto *top = new QWidget(m_splitter);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->addWidget(m_commitList, 1);
    auto *listButtons = new QHBoxLayout;
    const QStringList entries = splitAllTexts();
    auto *splitAllMenu = new QMenu(m_splitAllOptions);
    for (int entry = 0; entry < entries.size(); ++entry) {
        splitAllMenu->addAction(entries.at(entry), this, [this, entry] {
            onSplitAllOptions(entry);
        });
    }
    m_splitAllOptions->setMenu(splitAllMenu);
    m_splitAllOptions->setPopupMode(QToolButton::MenuButtonPopup);
    m_splitAllOptions->setText(entries.constFirst());
    m_splitAllOptions->setToolButtonStyle(Qt::ToolButtonTextOnly);
    listButtons->addWidget(m_splitAllOptions);
    listButtons->addWidget(m_upButton);
    listButtons->addWidget(m_downButton);
    listButtons->addStretch();
    listButtons->addWidget(m_preserveMergesCheck);
    listButtons->addWidget(m_forceCheck);
    topLayout->addLayout(listButtons);
    m_splitter->addWidget(top);

    m_tabs->addTab(m_fileListCtrl, i18n("Revision Files"));
    m_tabs->addTab(m_logMessageCtrl, i18n("Commit Message"));
    // The Log tab is added when the rebase starts
    m_outputRebase->hide();
    m_splitter->addWidget(m_tabs);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    layout->addWidget(m_splitter, 1);

    m_progressBar->setTextVisible(false);
    m_progressBar->setMaximumHeight(m_progressBar->fontMetrics().height() / 2 + 2);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    auto *bottomRow = new QHBoxLayout;
    m_statusLabel->setAutoFillBackground(true);
    bottomRow->addWidget(m_statusLabel, 1);
    m_continueButton->setDefault(true);
    bottomRow->addWidget(m_continueButton);
    bottomRow->addWidget(m_abortButton);
    layout->addLayout(bottomRow);

    // The tooltips of OnInitDialog
    m_forceCheck->setToolTip(
        i18n("Force the rebase even if the current branch is a descendant of the commit you are rebasing onto.\nNormally the "
             "command will exit with the message \"Current branch is up to date\" in such a situation."));
    m_abortButton->setToolTip(i18n("Recover to the status before rebase"));
    m_preserveMergesCheck->setToolTip(
        i18n("This preserves merge commits, however, it has known bugs as re-ordering commits is not working properly (same "
             "limitation as for vanilla git rebase), see help."));

    connect(m_continueButton, &QPushButton::clicked, this, &RebaseDlg::onContinue);
    connect(m_abortButton, &QPushButton::clicked, this, &RebaseDlg::onAbort);
    connect(m_reverseButton, &QToolButton::clicked, this, &RebaseDlg::onReverse);
    connect(m_browseButton, &QPushButton::clicked, this, &RebaseDlg::onBrowse);
    connect(m_ontoButton, &QPushButton::clicked, this, &RebaseDlg::onOnto);
    connect(m_forceCheck, &QCheckBox::toggled, this, &RebaseDlg::onOptionToggled);
    connect(m_preserveMergesCheck, &QCheckBox::toggled, this, &RebaseDlg::onOptionToggled);
    connect(m_splitAllOptions, &QToolButton::clicked, this, [this] {
        onSplitAllOptions(m_splitAllEntry);
    });
    connect(m_upButton, &QPushButton::clicked, this, [this] {
        moveSelectedUp(QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
    });
    connect(m_downButton, &QPushButton::clicked, this, [this] {
        moveSelectedDown(QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
    });
    connect(m_commitList, &GitLogList::loaded, this, &RebaseDlg::onLogListLoaded);
    connect(m_commitList, &GitLogList::loadFailed, this, [this] {
        m_continueButton->setEnabled(false);
        Q_EMIT logListFetched();
    });
    connect(m_commitList, &GitLogList::selectedRevChanged, this, &RebaseDlg::onCommitSelectionChanged);
    connect(m_commitList, &GitLogList::filesLoaded, this, [this](int row) {
        if (row == m_commitList->selectedRow()) {
            refreshFileList();
        }
    });
    connect(m_commitList, &GitLogList::rebaseActionChanged, this, &RebaseDlg::onRebaseActionChanged);
    m_commitList->installEventFilter(this);

    // PreTranslateMessage's Ctrl+Return
    const auto *continueShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(continueShortcut, &QShortcut::activated, this, [this] {
        if (m_continueButton->isEnabled()) {
            onContinue();
        } else if (m_abortButton->isEnabled()) {
            m_abortButton->setFocus();
        }
    });
    // PreTranslateMessage's VK_F5, wherever the focus is
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, &RebaseDlg::refresh);
}

RebaseDlg::RebaseStage RebaseDlg::stage() const
{
    return m_rebaseStage;
}

bool RebaseDlg::isThreadRunning() const
{
    return m_threadRunning;
}

bool RebaseDlg::isLoading() const
{
    return !m_branchesLoaded || m_commitList->isBusy();
}

QComboBox *RebaseDlg::branchCombo() const
{
    return m_branchCtrl;
}

QComboBox *RebaseDlg::upstreamCombo() const
{
    return m_upstreamCtrl;
}

QCheckBox *RebaseDlg::forceCheck() const
{
    return m_forceCheck;
}

QCheckBox *RebaseDlg::preserveMergesCheck() const
{
    return m_preserveMergesCheck;
}

GitLogList *RebaseDlg::commitList() const
{
    return m_commitList;
}

GitStatusListCtrl *RebaseDlg::fileList() const
{
    return m_fileListCtrl;
}

QPlainTextEdit *RebaseDlg::messageEdit() const
{
    return m_logMessageCtrl;
}

QPlainTextEdit *RebaseDlg::logOutput() const
{
    return m_outputRebase;
}

QTabWidget *RebaseDlg::tabs() const
{
    return m_tabs;
}

QPushButton *RebaseDlg::continueButton() const
{
    return m_continueButton;
}

QPushButton *RebaseDlg::abortButton() const
{
    return m_abortButton;
}

QLabel *RebaseDlg::statusLabel() const
{
    return m_statusLabel;
}

void RebaseDlg::loadBranchInfo()
{
    // LoadBranchInfo's lists, read in the background as ChooseVersion reads them
    m_branchesLoaded = false;
    m_branchCall = m_git.runAsync(Git::branchListArguments(BranchAllF), RunMode::ReadOnly);
    m_tagCall = m_git.runAsync({QStringLiteral("tag"), QStringLiteral("-l")}, RunMode::ReadOnly);
    connect(m_branchCall.get(), &GitCall::finished, this, &RebaseDlg::onBranchesLoaded);
    connect(m_tagCall.get(), &GitCall::finished, this, &RebaseDlg::onBranchesLoaded);
}

void RebaseDlg::onBranchesLoaded()
{
    if (!m_branchCall || !m_tagCall || !m_branchCall->isFinished() || !m_tagCall->isFinished()) {
        return;
    }
    // Deleted from the event loop: one of them is still emitting
    GitCall *branchCall = m_branchCall.release();
    GitCall *tagCall = m_tagCall.release();
    branchCall->deleteLater();
    tagCall->deleteLater();
    const QByteArray branchOutput = AppUtils::outputOrEmpty(*branchCall);

    // LoadBranchInfo
    {
        const QSignalBlocker branchBlocker(m_branchCtrl);
        const QSignalBlocker upstreamBlocker(m_upstreamCtrl);
        const BranchList branches = Git::parseBranchList(branchOutput, BranchAll, false, false);
        m_branchCtrl->clear();
        m_branchCtrl->addItems(branches.branches);
        if (branches.current >= 0) {
            m_branchCtrl->setCurrentIndex(branches.current);
        } else {
            // GetCurrentBranch(true): HEAD when detached
            const QString head = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(m_git.workingDirectory()));
            addString(m_branchCtrl, head.isEmpty() ? QStringLiteral("HEAD") : head);
        }

        const BranchList upstreams = Git::parseBranchList(branchOutput, BranchAllF, false, !m_git.derefFetchHead().isEmpty());
        m_upstreamCtrl->clear();
        m_upstreamCtrl->addItems(upstreams.branches + Git::parseTagList(AppUtils::outputOrEmpty(*tagCall)));
        m_upstreamCtrl->setCurrentIndex(-1);
        if (!m_options.upstream.isEmpty()) {
            addString(m_upstreamCtrl, m_options.upstream);
        } else {
            // Select pull-remote from current branch (GetRemoteTrackedBranchForHEAD)
            const QString current = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(m_git.workingDirectory()));
            const QString pullRemote = m_git.configValue(QStringLiteral("branch.%1.remote").arg(current));
            const QString pullBranch = Git::stripRefName(m_git.configValue(QStringLiteral("branch.%1.merge").arg(current)));
            const QString defaultUpstream = QStringLiteral("remotes/%1/%2").arg(pullRemote, pullBranch);
            m_upstreamCtrl->setCurrentIndex(m_upstreamCtrl->findText(defaultUpstream, Qt::MatchExactly | Qt::MatchCaseSensitive));
        }
    }
    m_branchesLoaded = true;
    addBranchToolTips(m_branchCtrl);
    addBranchToolTips(m_upstreamCtrl);
    connect(m_branchCtrl, &QComboBox::currentIndexChanged, this, &RebaseDlg::fetchLogList, Qt::UniqueConnection);
    connect(m_upstreamCtrl, &QComboBox::currentIndexChanged, this, &RebaseDlg::fetchLogList, Qt::UniqueConnection);
    fetchLogList();
}

bool RebaseDlg::checkFetchLogListRefs(QString& branchHash, QString& upstreamHash)
{
    const QString branch = m_branchCtrl->currentText();
    const QString upstream = m_upstreamCtrl->currentText();
    if (branch.isEmpty()) {
        m_commitList->showText(i18n("Please select branch"));
        return false;
    }
    branchHash = hashOf(branch);
    if (branchHash.isEmpty()) {
        m_commitList->showText(i18n("Could not get hash of \"%1\".", branch));
        return false;
    }
    if (upstream.isEmpty()) {
        m_commitList->showText(i18n("Please select upstream"));
        return false;
    }
    upstreamHash = hashOf(upstream);
    if (upstreamHash.isEmpty()) {
        m_commitList->showText(i18n("Could not get hash of \"%1\".", upstream));
        return false;
    }
    if (branchHash == upstreamHash) {
        m_commitList->showText(i18n("No need to rebase\n\"%1\" equals \"%2\"", branch, upstream));
        return false;
    }
    return true;
}

void RebaseDlg::fetchLogList()
{
    // FetchLogList
    m_isFastForward = false;
    m_continueButton->setEnabled(false);
    QString branchHash;
    QString upstreamHash;
    if (!checkFetchLogListRefs(branchHash, upstreamHash)) {
        Q_EMIT logListFetched();
        return;
    }
    const QString branch = m_branchCtrl->currentText();
    const QString upstream = m_upstreamCtrl->currentText();

    QString base;
    if (isFastForward(branch, upstream, &base) && m_onto.isEmpty()) {
        m_isFastForward = true;
        m_commitList->showText(i18n("Branch \"%1\" behind \"%2\"\n\"%3\" will fastforward to \"%4\"", branch, upstream, branch, upstream));
        m_continueButton->setEnabled(true);
        setContinueButtonText();
        Q_EMIT logListFetched();
        return;
    }

    if (!m_forceCheck->isChecked() && m_onto.isEmpty() && base == upstreamHash) {
        m_commitList->showText(
            i18n("Current branch \"%1\" is up to date\n\nIf you want to force a rebase even if the current branch is a descendant of the commit you "
                 "are rebasing onto, please check below \"Force Rebase\" checkbox.",
                 branch));
        setContinueButtonText();
        Q_EMIT logListFetched();
        return;
    }

    // The commits upstream..branch, oldest last, in the background (FillGitLog with LOG_ORDER_TOPOORDER)
    const QString range = QStringLiteral("%1..%2").arg(m_git.fixBranchName(upstream), m_git.fixBranchName(branch));
    m_commitList->setRange(range, !m_preserveMergesCheck->isChecked());
    m_commitList->reload();
}

void RebaseDlg::onLogListLoaded()
{
    // The rest of FetchLogList, once the commits are in
    GitLogListModel *model = m_commitList->logModel();
    if (model->rowCount() == 0) {
        m_commitList->showText(i18n("Nothing to Rebase"));
    }

    m_rewrittenCommitsMap.clear();
    m_droppedCommitsMap.clear();
    const bool preserveMerges = m_preserveMergesCheck->isChecked();
    if (preserveMerges) {
        preserveMergesDropCommits();
    }

    addBranchToolTips(m_branchCtrl);
    addBranchToolTips(m_upstreamCtrl);

    bool hasSkip = false;
    if (!preserveMerges) {
        // Default all actions to 'pick'
        for (int row = 0; row < model->rowCount(); ++row) {
            model->setRebaseAction(row, GitRevLoglist::RebasePick);
        }
        hasSkip = markCherryPickedAsSkip();
    }
    if (hasSkip) {
        setStatusText(i18n("Please take a look at the skipped commits"), true);
    } else {
        setStatusText(m_statusText, false);
    }

    m_currentRebaseIndex = model->rowCount();
    m_continueButton->setEnabled(preserveMerges || model->rowCount() > 0);
    setContinueButtonText();
    Q_EMIT logListFetched();
}

void RebaseDlg::preserveMergesDropCommits()
{
    // FetchLogList's m_bPreserveMerges part: keep the commits that descend from the merge base
    GitLogListModel *model = m_commitList->logModel();
    const QString head = hashOf(QStringLiteral("HEAD"));
    if (head.isEmpty()) {
        addLogString(i18n("No HEAD found"));
        return;
    }
    const QString upstreamRef = m_onto.isEmpty() ? m_upstreamCtrl->currentText() : m_onto;
    const QString upstreamHash = hashOf(upstreamRef);
    if (upstreamHash.isEmpty()) {
        showError(i18n("Could not get hash of \"%1\".", upstreamRef));
        return;
    }
    const GitResult mergeBases = gitRun({QStringLiteral("merge-base"), QStringLiteral("--all"), head, upstreamHash});
    for (const QString& line : mergeBases.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (mergeBases.ok) {
            m_rewrittenCommitsMap.insert(line.trimmed(), upstreamHash);
        }
    }

    QList<int> toDrop;
    for (int row = model->rowCount() - 1; row >= 0; --row) {
        const GitRevLoglist& rev = model->rev(row);
        const bool preserve = std::ranges::any_of(rev.parents, [this](const QString& parent) {
            return m_rewrittenCommitsMap.contains(parent);
        });
        if (preserve) {
            m_rewrittenCommitsMap.insert(rev.hash, QString());
        } else {
            toDrop.append(row);
        }
    }

    // Drop already included commits
    const QString range = QStringLiteral("%1...%2").arg(m_git.fixBranchName(m_upstreamCtrl->currentText()), m_git.fixBranchName(m_branchCtrl->currentText()));
    const GitResult cherry = gitRun({QStringLiteral("rev-list"), range, QStringLiteral("--left-right"), QStringLiteral("--cherry-pick")});
    QStringList nonCherryPicked;
    for (const QString& line : cherry.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (cherry.ok && line.startsWith(QLatin1Char('>'))) {
            nonCherryPicked.append(line.mid(1).trimmed());
        }
    }
    for (int row = model->rowCount() - 1; row >= 0; --row) {
        const GitRevLoglist& rev = model->rev(row);
        model->setRebaseAction(row, GitRevLoglist::RebasePick);
        if (m_rewrittenCommitsMap.contains(rev.hash) && !nonCherryPicked.contains(rev.hash)) {
            m_droppedCommitsMap.insert(rev.hash, {rev.parents.value(0)});
            toDrop.append(row);
            m_rewrittenCommitsMap.remove(rev.hash);
        }
    }
    std::ranges::sort(toDrop);
    toDrop.erase(std::unique(toDrop.begin(), toDrop.end()), toDrop.end());
    for (auto it = toDrop.crbegin(); it != toDrop.crend(); ++it) {
        model->removeRev(*it);
    }
}

bool RebaseDlg::markCherryPickedAsSkip()
{
    // Default to skip when already in upstream (git cherry marks those with a '-')
    GitLogListModel *model = m_commitList->logModel();
    const QString from = m_git.fixBranchName(m_onto.isEmpty() ? m_upstreamCtrl->currentText() : m_onto);
    const QString branchRef = m_git.fixBranchName(m_branchCtrl->currentText());
    const GitResult cherry = gitRun({QStringLiteral("cherry"), QStringLiteral("--end-of-options"), from, branchRef});
    if (!cherry.ok) {
        return false;
    }
    bool hasSkip = false;
    for (const QString& line : cherry.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (!line.startsWith(QLatin1String("- "))) {
            continue;
        }
        const QString hash = line.mid(2).trimmed();
        for (int row = 0; row < model->rowCount(); ++row) {
            if (model->rev(row).hash == hash) {
                model->setRebaseAction(row, GitRevLoglist::RebaseSkip);
                hasSkip = true;
                break;
            }
        }
    }
    return hasSkip;
}

void RebaseDlg::addBranchToolTips(QComboBox *combo)
{
    // AddBranchToolTips
    combo->setToolTip({});
    const QString text = combo->currentText();
    if (text.isEmpty()) {
        return;
    }
    const std::optional<GitRevLoglist> rev = getCommit(text);
    if (!rev) {
        return;
    }
    QString toolTip = QStringLiteral("%1: %2\n%3: %4 <%5>\n%6: %7\n%8:\n%9\n%10")
                          .arg(i18n("Revision"), rev->hash, i18n("Author"), rev->authorName, rev->authorEmail)
                          .arg(i18n("Date"), QLocale().toString(rev->authorDate, QLocale::LongFormat), i18n("Message"), rev->subject, rev->body);
    if (toolTip.size() > maxToolTipLength) {
        toolTip.truncate(maxToolTipLength);
        toolTip += QStringLiteral("...");
    }
    combo->setToolTip(toolTip);
}

void RebaseDlg::setAllRebaseAction(unsigned int action)
{
    // SetAllRebaseAction: the last commit and merge commits can't be squashed
    GitLogListModel *model = m_commitList->logModel();
    for (int row = 0; row < model->rowCount(); ++row) {
        if (action == GitRevLoglist::RebaseSquash && (row == model->rowCount() - 1 || model->rev(row).parents.size() != 1)) {
            continue;
        }
        model->setRebaseAction(row, action);
    }
}

void RebaseDlg::onSplitAllOptions(int entry)
{
    // OnBnClickedSplitAllOptions; picking an entry from the menu makes it the button's own, as CMenuButton does
    m_splitAllEntry = entry;
    m_splitAllOptions->setText(splitAllTexts().value(entry));
    if (m_threadRunning) {
        return;
    }
    switch (entry) {
    case PickAll:
        setAllRebaseAction(GitRevLoglist::RebasePick);
        break;
    case SquashAll:
        setAllRebaseAction(GitRevLoglist::RebaseSquash);
        break;
    case EditAll:
        setAllRebaseAction(GitRevLoglist::RebaseEdit);
        break;
    case SkipUnselected:
        m_commitList->setUnselectedRebaseAction(GitRevLoglist::RebaseSkip);
        break;
    case SquashUnselected:
        m_commitList->setUnselectedRebaseAction(GitRevLoglist::RebaseSquash);
        break;
    case EditUnselected:
        m_commitList->setUnselectedRebaseAction(GitRevLoglist::RebaseEdit);
        break;
    default:
        Q_ASSERT(false);
    }
}

void RebaseDlg::moveSelectedUp(bool toTop)
{
    // OnBnClickedButtonUp
    QList<int> rows;
    for (const QModelIndex& index : m_commitList->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::ranges::sort(rows);
    // Do nothing if the first selected item is the first item in the list
    if (rows.isEmpty() || (!toTop && rows.constFirst() == 0)) {
        return;
    }
    GitLogListModel *model = m_commitList->logModel();
    int count = 0;
    bool changed = false;
    for (int index : std::as_const(rows)) {
        count = toTop ? count : index - 1;
        while (index > count) {
            model->swapWithNext(index - 1);
            changed = true;
            --index;
        }
        ++count;
    }
    if (changed) {
        m_commitList->scrollTo(m_commitList->selectionModel()->selectedRows().constFirst());
        m_commitList->setFocus();
    }
}

void RebaseDlg::moveSelectedDown(bool toBottom)
{
    // OnBnClickedButtonDown
    QList<int> rows;
    for (const QModelIndex& index : m_commitList->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::ranges::sort(rows);
    GitLogListModel *model = m_commitList->logModel();
    const int lastRow = model->rowCount() - 1;
    // Don't move anything if the last selected item is the last one: that would change the selection's order
    if (rows.isEmpty() || (!toBottom && rows.constLast() >= lastRow)) {
        return;
    }
    int count = lastRow;
    bool changed = false;
    // Backwards, so several selected items move correctly
    for (auto it = rows.crbegin(); it != rows.crend(); ++it) {
        int index = *it;
        count = toBottom ? count : index + 1;
        while (index < count) {
            model->swapWithNext(index);
            changed = true;
            ++index;
        }
        --count;
    }
    if (changed) {
        m_commitList->scrollTo(model->index(std::min(rows.constLast() + 1, lastRow), 0));
        m_commitList->setFocus();
    }
}

void RebaseDlg::onReverse()
{
    // OnBnClickedButtonReverse
    const QString branch = m_branchCtrl->currentText();
    const QString upstream = m_upstreamCtrl->currentText();
    {
        const QSignalBlocker branchBlocker(m_branchCtrl);
        const QSignalBlocker upstreamBlocker(m_upstreamCtrl);
        addString(m_branchCtrl, upstream);
        addString(m_upstreamCtrl, branch);
    }
    fetchLogList();
}

void RebaseDlg::onBrowse()
{
    // OnBnClickedButtonBrowse: CBrowseRefsDlg::PickRefForCombo
    const QString ref = BrowseRefsDlg::pickRef(m_git.workingDirectory(), {.initialRef = m_upstreamCtrl->currentText(), .pickRefKind = PickRefAll}, this);
    if (ref.isEmpty()) {
        return;
    }
    {
        const QSignalBlocker blocker(m_upstreamCtrl);
        addString(m_upstreamCtrl, Git::stripRefName(ref));
    }
    fetchLogList();
}

void RebaseDlg::onOnto()
{
    // OnBnClickedButtonOnto
    m_onto = BrowseRefsDlg::pickRef(m_git.workingDirectory(), {.initialRef = m_onto, .pickRefKind = PickRefAll}, this);
    if (!m_onto.isEmpty()) {
        // Make sure that the user did not select upstream or the selected branch
        const QString hash = hashOf(m_onto);
        if (hash.isEmpty()) {
            showError(i18n("Could not get hash of \"%1\".", m_onto));
            m_onto.clear();
        } else {
            const auto sameAs = [this, &hash](const QString& ref) {
                const QString refHash = hashOf(ref);
                return refHash.isEmpty() || refHash == hash;
            };
            if (sameAs(m_upstreamCtrl->currentText()) || sameAs(m_branchCtrl->currentText())) {
                m_onto.clear();
            }
        }
    }
    m_ontoButton->setToolTip(m_onto);
    m_ontoButton->setChecked(!m_onto.isEmpty());
    fetchLogList();
}

void RebaseDlg::onOptionToggled()
{
    // OnBnClickedRebaseCheckForce, for both checkboxes
    fetchLogList();
}

void RebaseDlg::onCommitSelectionChanged()
{
    // FillLogMessageCtrl
    const RebaseStage stage = m_rebaseStage;
    const int row = m_commitList->selectedRow();
    if (row < 0 || (stage != RebaseStage::ChooseBranch && stage != RebaseStage::ChooseCommitPickMode)) {
        return;
    }
    const GitRevLoglist& rev = m_commitList->logModel()->rev(row);
    refreshFileList();
    m_logMessageCtrl->setPlainText(rev.subject + QLatin1Char('\n') + rev.body);
}

void RebaseDlg::refreshFileList()
{
    // OnRefreshFilelist
    const RebaseStage stage = m_rebaseStage;
    const int row = m_commitList->selectedRow();
    if (row < 0 || (stage != RebaseStage::ChooseBranch && stage != RebaseStage::ChooseCommitPickMode)) {
        return;
    }
    const GitRevLoglist& rev = m_commitList->logModel()->rev(row);
    if (rev.filesState != GitRevLoglist::FilesState::Loaded) {
        m_fileListCtrl->clear();
        m_fileListCtrl->setBusyString(i18n("Fetching changed files..."));
        m_fileListCtrl->setBusy(true);
        m_commitList->requestFiles(row);
        return;
    }
    m_fileListCtrl->setCurrentVersion(rev.hash, rev.parents);
    m_fileListCtrl->updateWithGitPathList(GitStatusListModel::makeRevisionEntries(rev.files));
}

void RebaseDlg::onRebaseActionChanged()
{
    // OnRebaseActionMessage: skipping the commit that stopped on a conflict or an error throws away its changes
    const RebaseStage stage = m_rebaseStage;
    if (m_threadRunning || (stage != RebaseStage::Error && stage != RebaseStage::Conflict)) {
        return;
    }
    if (m_currentRebaseIndex < 0 || m_currentRebaseIndex >= m_commitList->logModel()->rowCount()) {
        return;
    }
    if ((currentRev().rebaseAction & GitRevLoglist::RebaseModeMask) != GitRevLoglist::RebaseSkip) {
        return;
    }
    startThread([this] {
        if (runGitCmdRetryOrAbort({QStringLiteral("reset"), QStringLiteral("--hard")})) {
            runOnUi([this] {
                m_fileListCtrl->clear();
            });
            m_rebaseStage = RebaseStage::Continue;
        }
    });
}

void RebaseDlg::refresh()
{
    // Refresh
    const RebaseStage stage = m_rebaseStage;
    if (stage == RebaseStage::Conflict || stage == RebaseStage::SquashConflict) {
        listConflictFile();
        return;
    }
    if (stage == RebaseStage::ChooseBranch && !m_threadRunning) {
        loadBranchInfo();
    }
}

bool RebaseDlg::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_commitList && event->type() == QEvent::KeyPress) {
        const auto *keyEvent = dynamic_cast<const QKeyEvent *>(event);
        if (keyEvent && handleListKey(keyEvent->key(), keyEvent->modifiers())) {
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

bool RebaseDlg::handleListKey(int key, Qt::KeyboardModifiers modifiers)
{
    // PreTranslateMessage's keys for the commit list
    const bool mayChange = m_commitList->isRebaseMenuEnabled() && modifiers == Qt::NoModifier;
    switch (key) {
    case Qt::Key_Space:
        if (mayChange) {
            m_commitList->shiftSelectedRebaseAction();
        }
        return mayChange;
    case Qt::Key_P:
    case Qt::Key_S:
    case Qt::Key_Q:
    case Qt::Key_E: {
        if (!mayChange) {
            return false;
        }
        const unsigned int action = key == Qt::Key_P ? GitRevLoglist::RebasePick
            : key == Qt::Key_S                       ? GitRevLoglist::RebaseSkip
            : key == Qt::Key_Q                       ? GitRevLoglist::RebaseSquash
                                                     : GitRevLoglist::RebaseEdit;
        m_commitList->setSelectedRebaseAction(action);
        return true;
    }
    case Qt::Key_U:
    case Qt::Key_D: {
        const QPushButton *button = key == Qt::Key_U ? m_upButton : m_downButton;
        if (!button->isEnabled() || (modifiers & ~Qt::ShiftModifier)) {
            return false;
        }
        if (key == Qt::Key_U) {
            moveSelectedUp(modifiers.testFlag(Qt::ShiftModifier));
        } else {
            moveSelectedDown(modifiers.testFlag(Qt::ShiftModifier));
        }
        return true;
    }
    default:
        return false;
    }
}

void RebaseDlg::setContinueButtonText()
{
    // SetContinueButtonText
    QString text;
    switch (m_rebaseStage.load()) {
    case RebaseStage::ChooseBranch:
    case RebaseStage::ChooseCommitPickMode:
        text = m_isFastForward ? i18n("Start (FastFwd)") : i18n("Start Rebase");
        break;
    case RebaseStage::Start:
    case RebaseStage::Error:
    case RebaseStage::Continue:
    case RebaseStage::SquashConflict:
        text = i18n("Continue");
        break;
    case RebaseStage::Conflict:
    case RebaseStage::SquashEdit:
        text = i18n("Commit");
        break;
    case RebaseStage::Edit:
        text = i18n("Amend");
        break;
    case RebaseStage::Abort:
    case RebaseStage::Finish:
        text = i18n("Finish");
        break;
    case RebaseStage::Done:
        text = i18n("Done");
        break;
    }
    m_continueButton->setText(text);
}

void RebaseDlg::setControlEnable()
{
    // SetControlEnable
    const RebaseStage stage = m_rebaseStage;
    const bool choosing = stage == RebaseStage::ChooseBranch || stage == RebaseStage::ChooseCommitPickMode;
    for (QWidget *widget : std::initializer_list<QWidget *>{m_splitAllOptions,
                                                            m_upButton,
                                                            m_downButton,
                                                            m_branchCtrl,
                                                            m_upstreamCtrl,
                                                            m_reverseButton,
                                                            m_forceCheck,
                                                            m_preserveMergesCheck,
                                                            m_browseButton,
                                                            m_ontoButton}) {
        widget->setEnabled(choosing && !m_threadRunning);
    }
    // The actions stay changeable while the rebase is stopped, as TortoiseGit's menu mask does, but not while
    // the worker reads them
    m_commitList->setRebaseMenuEnabled(!m_threadRunning);

    if (m_threadRunning) {
        m_continueButton->setEnabled(false);
    } else if (stage != RebaseStage::Error) {
        m_continueButton->setEnabled(true);
    }
    m_abortButton->setEnabled(true);
}

void RebaseDlg::updateProgress()
{
    // UpdateProgress
    GitLogListModel *model = m_commitList->logModel();
    const int itemCount = model->rowCount();
    const int index = itemCount - m_currentRebaseIndex;
    int finishedCommits = index - 1; // The number of commits handled, for the progress bar
    if (m_rebaseStage == RebaseStage::Finish || finishedCommits == -1) {
        finishedCommits = index;
    }
    m_progressBar->setRange(0, itemCount);
    m_progressBar->setValue(std::clamp(finishedCommits, 0, itemCount));

    const bool hasCurrent = m_currentRebaseIndex >= 0 && m_currentRebaseIndex < itemCount;
    if (hasCurrent) {
        m_statusText = i18n("Rebasing... (%1/%2)", index, itemCount);
        setStatusText(m_statusText, false);
    }
    for (int row = 0; row < itemCount; ++row) {
        const unsigned int action = model->rev(row).rebaseAction;
        if (action & GitRevLoglist::RebaseCurrent) {
            model->setRebaseAction(row, action & ~GitRevLoglist::RebaseCurrent);
        }
    }
    if (hasCurrent) {
        model->setRebaseAction(m_currentRebaseIndex, model->rev(m_currentRebaseIndex).rebaseAction | GitRevLoglist::RebaseCurrent);
        m_commitList->scrollTo(model->index(m_currentRebaseIndex, 0));
    }
}

void RebaseDlg::updateCurrentStatus()
{
    // UpdateCurrentStatus
    setContinueButtonText();
    setControlEnable();
    updateProgress();
    if (m_rebaseStage == RebaseStage::Done) {
        m_continueButton->setFocus();
    }
}

void RebaseDlg::setStatusText(const QString& text, bool warning)
{
    // OnCtlColor: white on red for a warning
    m_statusLabel->setText(text);
    QPalette palette = this->palette();
    if (warning) {
        palette.setColor(QPalette::Window, Qt::red);
        palette.setColor(QPalette::WindowText, Qt::white);
    }
    m_statusLabel->setPalette(palette);
}

void RebaseDlg::onRebaseUpdateUi()
{
    // OnRebaseUpdateUI; TortoiseGit also finishes the rebase here, which the worker does itself
    updateCurrentStatus();
    if (m_currentRebaseIndex < 0 || m_currentRebaseIndex >= m_commitList->logModel()->rowCount()) {
        return;
    }
    showStageMessage();
}

void RebaseDlg::showStageMessage()
{
    const GitRevLoglist rev = currentRev();
    switch (m_rebaseStage.load()) {
    case RebaseStage::Conflict:
    case RebaseStage::SquashConflict:
        listConflictFile();
        m_tabs->setCurrentIndex(ConflictTab);
        m_logMessageCtrl->setReadOnly(false);
        m_logMessageCtrl->setPlainText(rev.subject + QLatin1Char('\n') + rev.body);
        break;
    case RebaseStage::Edit:
        m_tabs->setCurrentIndex(MessageTab);
        m_logMessageCtrl->setReadOnly(false);
        m_logMessageCtrl->setPlainText(rev.subject + QLatin1Char('\n') + rev.body);
        break;
    case RebaseStage::SquashEdit:
        m_tabs->setCurrentIndex(MessageTab);
        m_logMessageCtrl->setReadOnly(false);
        m_logMessageCtrl->setPlainText(m_squashMessage);
        break;
    default:
        setActiveTab(LogTab);
    }
}

void RebaseDlg::listConflictFile()
{
    // ListConflictFile
    m_fileListCtrl->setHasCheckboxes(true);
    writeRebaseActiveFolder();
    m_fileListCtrl->setCurrentVersion({}, {});
    const GitStatusListModel::ShowFlags show = Show::Conflicted | Show::Modified | Show::Added | Show::Removed | Show::Replaced;
    // Check(GITSLC_SHOWFILES): every file listed starts checked
    m_fileListCtrl->setShowFlags(show, show);
    m_fileListCtrl->reload();
    // TortoiseGit warns about submodule changes here; submodules are not listed yet
    setStatusText(m_statusText, false);
}

void RebaseDlg::selectFirstConflict()
{
    // VerifyNoConflict: select the first conflicted file
    m_tabs->setCurrentIndex(ConflictTab);
    m_fileListCtrl->selectFirstConflict();
}

void RebaseDlg::writeRebaseActiveFolder()
{
    // ListConflictFile: the lock that CAppUtils::IsTGitRebaseActive looks for
    const QString adminDir = GitAdminDir::worktreeAdminDirPath(m_git.workingDirectory());
    if (adminDir.isEmpty()) {
        return;
    }
    const QDir dir(QDir(adminDir).filePath(QStringLiteral("tgitrebase.active")));
    if (!dir.mkpath(QStringLiteral("."))) {
        return;
    }
    const auto write = [&dir](const QString& name, const QString& text) {
        QFile file(dir.filePath(name));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(text.toUtf8());
        }
    };
    write(QStringLiteral("head-name"), m_branch);
    write(QStringLiteral("onto"), m_onto.isEmpty() ? m_upstream : m_onto);
}

void RebaseDlg::cleanUpRebaseActiveFolder()
{
    // CleanUpRebaseActiveFolder
    const QString adminDir = GitAdminDir::worktreeAdminDirPath(m_git.workingDirectory());
    if (adminDir.isEmpty()) {
        return;
    }
    QDir dir(QDir(adminDir).filePath(QStringLiteral("tgitrebase.active")));
    if (!dir.exists()) {
        return;
    }
    dir.remove(QStringLiteral("head-name"));
    dir.remove(QStringLiteral("onto"));
    std::ignore = dir.rmdir(dir.absolutePath());
}

void RebaseDlg::checkRestoreStash()
{
    // CheckRestoreStash with CAppUtils::StashPop; TortoiseGit's offer to show the changes needs repostatus
    const QString autoStashValue = m_git.configValue(QStringLiteral("rebase.autostash")).toLower();
    const bool autoStash = autoStashValue == QLatin1String("true") || autoStashValue == QLatin1String("yes") || autoStashValue == QLatin1String("on")
        || autoStashValue == QLatin1String("1");
    if (m_stashed
        && (autoStash
            || QMessageBox::question(this, i18n("KortoiseGit"), i18n("Do you want to stash pop now?"), QMessageBox::Yes | QMessageBox::No)
                == QMessageBox::Yes)) {
        const GitResult result = gitRun({QStringLiteral("stash"), QStringLiteral("pop")});
        const bool hasConflicts = result.output.contains(QLatin1String("CONFLICT"));
        if (!result.ok && !hasConflicts) {
            QMessageBox::critical(this, i18n("KortoiseGit"), i18n("Stash POP failed!") + QLatin1Char('\n') + result.output);
        } else if (hasConflicts) {
            QMessageBox::warning(this, i18n("KortoiseGit"), i18n("Stash POP failed, there are conflicts"));
        }
    }
    m_stashed = false;
}

void RebaseDlg::finishDialog()
{
    // OnBnClickedContinue at Done
    QDialog::accept();
    cleanUpRebaseActiveFolder();
    checkRestoreStash();
}

void RebaseDlg::onContinue()
{
    if (m_threadRunning) {
        return;
    }
    if (m_rebaseStage == RebaseStage::Done) {
        finishDialog();
        return;
    }
    // What the worker works on: the controls are disabled from here on
    if (m_rebaseStage == RebaseStage::ChooseBranch) {
        m_branch = m_branchCtrl->currentText();
        m_upstream = m_upstreamCtrl->currentText();
        m_force = m_forceCheck->isChecked();
        m_preserveMerges = m_preserveMergesCheck->isChecked();
    }
    m_messageText = m_logMessageCtrl->toPlainText();
    startThread([this] {
        continueJob();
    });
}

void RebaseDlg::onAbort()
{
    // OnBnClickedAbort; a Revert or Resolved in the list has to finish first
    if (m_fileListCtrl->isModifying()) {
        return;
    }
    const auto askAbort = [this] {
        return QMessageBox::question(this, i18n("KortoiseGit"), i18n("Are you sure you want to abort the rebase process?"), QMessageBox::Yes | QMessageBox::No)
            == QMessageBox::Yes;
    };
    if (m_threadRunning) {
        if (askAbort()) {
            m_abortRequested = true;
        }
        return;
    }
    const RebaseStage stage = m_rebaseStage;
    if (stage == RebaseStage::ChooseBranch || stage == RebaseStage::ChooseCommitPickMode || m_origUpstreamHash.isEmpty() || m_origHeadHash.isEmpty()) {
        QDialog::reject();
        cleanUpRebaseActiveFolder();
        checkRestoreStash();
        return;
    }
    if (!m_abortRequested && !askAbort()) {
        return;
    }
    m_closeAfterThread = true;
    m_acceptAfterThread = false;
    startThread([this] {
        abortJob();
    });
}

void RebaseDlg::reject()
{
    // OnCancel
    onAbort();
}

void RebaseDlg::startThread(std::function<void()> job)
{
    // OnBnClickedContinue's AfxBeginThread(RebaseThreadEntry)
    Q_ASSERT(!m_thread);
    m_threadRunning = true;
    setControlEnable();
    m_thread = QThread::create(std::move(job));
    connect(m_thread, &QThread::finished, this, &RebaseDlg::onThreadFinished);
    m_thread->start();
}

void RebaseDlg::onThreadFinished()
{
    // The end of RebaseThread
    m_thread->deleteLater();
    m_thread = nullptr;
    m_threadRunning = false;
    onRebaseUpdateUi();
    if (std::exchange(m_closeAfterThread, false)) {
        if (m_acceptAfterThread) {
            QDialog::accept();
        } else {
            QDialog::reject();
        }
        cleanUpRebaseActiveFolder();
        checkRestoreStash();
    } else if (m_abortRequested) {
        // RebaseThread posts the Abort click when asked to stop
        onAbort();
    }
    Q_EMIT threadFinished();
}

} // namespace kgt
