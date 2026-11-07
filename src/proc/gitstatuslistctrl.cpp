// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitStatusListCtrl.cpp

#include "gitstatuslistctrl.hpp"

#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "utils/runtimeerror.hpp"

#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QSortFilterProxyModel>
#include <QUrl>

#include <algorithm>
#include <iterator>
#include <utility>

namespace
{

using namespace kgt;
using Action = TGitPath::Action;

// The first status header when the branch has no commit yet: there is no HEAD
constexpr QByteArrayView initialBranchHeader = "# branch.oid (initial)";

/**
 * Takes a finished call out of its owner. It is deleted from the event loop, since it may still be emitting.
 * @param[in,out] call The owner; left empty.
 * @returns The call, valid until control returns to the event loop.
 */
GitCall *takeFinished(std::unique_ptr<GitCall>& call)
{
    GitCall *finished = call.release();
    Q_ASSERT(finished);
    finished->deleteLater();
    return finished;
}

/**
 * @param entry An entry.
 * @returns Whether git tracks the file, so it can be compared with HEAD.
 */
bool isTracked(const GitStatusListEntry& entry)
{
    return !(entry.actions & (Action::Unversioned | Action::Ignore));
}

/**
 * @param entry An entry.
 * @returns Whether the file is gone from the working tree, so it can't be opened.
 */
bool isGone(const GitStatusListEntry& entry)
{
    return (entry.actions & (Action::Deleted | Action::Missing)) != 0;
}

} // namespace

namespace kgt
{

GitStatusListCtrl::GitStatusListCtrl(QWidget *parent)
    : QTreeView(parent)
    , m_model(new GitStatusListModel(this))
    , m_proxy(new QSortFilterProxyModel(this))
{
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(GitStatusListModel::SortRole);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    setModel(m_proxy);

    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(GitStatusListModel::PathColumn, QHeaderView::Stretch);
    // Unsorted until the user clicks a header, as in TortoiseGit: git's order, tracked files first
    header()->setSortIndicator(-1, Qt::AscendingOrder);
    setSortingEnabled(true);

    connect(this, &QAbstractItemView::doubleClicked, this, &GitStatusListCtrl::onDoubleClicked);
    connect(m_model, &GitStatusListModel::checkedCountChanged, this, &GitStatusListCtrl::updateCheckedCount);
}

GitStatusListCtrl::~GitStatusListCtrl() = default;

void GitStatusListCtrl::setWorkingTree(const QString& root)
{
    m_workingTree = root;
}

const QString& GitStatusListCtrl::workingTree() const
{
    return m_workingTree;
}

void GitStatusListCtrl::setPathFilter(const QStringList& paths)
{
    m_pathFilter = paths;
}

void GitStatusListCtrl::setShowFlags(GitStatusListModel::ShowFlags show, GitStatusListModel::ShowFlags check)
{
    m_model->setShowFlags(show, check);
    const bool loadedBefore = m_statusCall || m_numStatCall || !m_model->allEntries().isEmpty();
    if (loadedBefore && show.testFlag(GitStatusListModel::Show::Ignored) && !m_hasIgnored) {
        reload();
    }
}

void GitStatusListCtrl::reload()
{
    Q_ASSERT(!m_workingTree.isEmpty());
    m_numStatCall.reset();
    m_errorMessage.clear();
    m_hasIgnored = m_model->showFlags().testFlag(GitStatusListModel::Show::Ignored);

    QStringList arguments = GitStatus::arguments(m_hasIgnored ? IgnoredFiles::Include : IgnoredFiles::Exclude);
    arguments.append(QStringLiteral("--branch"));
    arguments.append(pathSpec());
    m_statusCall = Git(m_workingTree).runAsync(arguments, RunMode::ReadOnly);
    connect(m_statusCall.get(), &GitCall::finished, this, &GitStatusListCtrl::onStatusFinished);
    viewport()->update();
}

bool GitStatusListCtrl::isBusy() const
{
    return m_statusCall || m_numStatCall;
}

void GitStatusListCtrl::setSelectButton(QCheckBox *button)
{
    m_selectButton = button;
    if (button) {
        button->setTristate(true);
        connect(button, &QCheckBox::clicked, this, &GitStatusListCtrl::onSelectButtonClicked);
    }
    updateCheckedCount();
}

void GitStatusListCtrl::setStatLabel(QLabel *label)
{
    m_statLabel = label;
    updateCheckedCount();
}

GitStatusListModel *GitStatusListCtrl::statusModel() const
{
    return m_model;
}

QList<GitStatusListEntry> GitStatusListCtrl::checkedEntries() const
{
    return m_model->checkedEntries();
}

QString GitStatusListCtrl::absolutePath(const GitStatusListEntry& entry) const
{
    return m_workingTree + QLatin1Char('/') + entry.path;
}

void GitStatusListCtrl::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F5 || event->matches(QKeySequence::Refresh)) {
        reload();
        return;
    }
    QTreeView::keyPressEvent(event);
}

void GitStatusListCtrl::contextMenuEvent(QContextMenuEvent *event)
{
    const QList<GitStatusListEntry> selected = selectedEntries();
    if (selected.isEmpty()) {
        return;
    }
    QMenu menu(this);
    QList<GitStatusListEntry> tracked;
    std::copy_if(selected.cbegin(), selected.cend(), std::back_inserter(tracked), isTracked);
    if (!tracked.isEmpty()) {
        QAction *compare = menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-diff")), i18n("Compare with b&ase"), this, [this, tracked] {
            for (const GitStatusListEntry& entry : tracked) {
                startDiff(entry);
            }
        });
        menu.setDefaultAction(compare);
    }
    if (selected.size() == 1) {
        const GitStatusListEntry& entry = selected.constFirst();
        if (!isGone(entry)) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("&Open"), this, [this, entry] {
                open(entry);
            });
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("Open with..."), this, [this, entry] {
                openWith(entry);
            });
        }
        if (!(entry.actions & Action::Deleted)) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Explore to"), this, [this, entry] {
                explore(entry);
            });
        }
    }
    if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
    }
}

void GitStatusListCtrl::paintEvent(QPaintEvent *event)
{
    QTreeView::paintEvent(event);
    if (m_model->rowCount() > 0) {
        return;
    }
    // TortoiseGit writes the list's state into the empty list (IDS_STATUSLIST_BUSYMSG, IDS_STATUSLIST_EMPTYMSG)
    QString message;
    if (isBusy()) {
        message = i18n("Please wait...");
    } else if (!m_errorMessage.isEmpty()) {
        message = m_errorMessage;
    } else {
        message = i18n("File list is empty");
    }
    QPainter painter(viewport());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(viewport()->rect().adjusted(8, 8, -8, -8), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, message);
}

void GitStatusListCtrl::onStatusFinished()
{
    const GitCall *call = takeFinished(m_statusCall);
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        failLoad(e.error());
        return;
    }
    Result<QList<GitStatusEntry>> status = GitStatus::parse(output);
    if (!status) {
        failLoad(status.error());
        return;
    }
    m_pendingStatus = std::move(*status);
    m_isInitialRepo = output.startsWith(initialBranchHeader);
    if (m_isInitialRepo) {
        // Nothing to count lines against; TortoiseGit lists an initial commit's files without counts too
        finishLoad({});
        return;
    }
    m_numStatCall = Git(m_workingTree).runAsync(GitNumStat::arguments(QStringLiteral("HEAD"), m_pathFilter), RunMode::ReadOnly);
    connect(m_numStatCall.get(), &GitCall::finished, this, &GitStatusListCtrl::onNumStatFinished);
}

void GitStatusListCtrl::onNumStatFinished()
{
    const GitCall *call = takeFinished(m_numStatCall);
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        failLoad(e.error());
        return;
    }
    Result<QList<NumStatEntry>> numStat = GitNumStat::parse(output);
    if (!numStat) {
        failLoad(numStat.error());
        return;
    }
    finishLoad(*numStat);
}

void GitStatusListCtrl::finishLoad(const QList<NumStatEntry>& numStat)
{
    m_model->setEntries(GitStatusListModel::makeEntries(m_pendingStatus, numStat));
    m_pendingStatus.clear();
    viewport()->update();
    Q_EMIT loaded();
}

void GitStatusListCtrl::failLoad(const Error& error)
{
    m_pendingStatus.clear();
    m_model->setEntries({});
    m_errorMessage = error.message;
    viewport()->update();
    Q_EMIT loadFailed(error);
}

void GitStatusListCtrl::updateCheckedCount()
{
    const int checked = m_model->checkedCount();
    const int total = m_model->rowCount();
    if (m_statLabel) {
        m_statLabel->setText(i18n("%1 files selected, %2 files total", checked, total));
    }
    if (m_selectButton) {
        Qt::CheckState state = Qt::PartiallyChecked;
        if (checked == 0) {
            state = Qt::Unchecked;
        } else if (checked == total) {
            state = Qt::Checked;
        }
        m_selectButton->setCheckState(state);
    }
    Q_EMIT checkedCountChanged();
}

void GitStatusListCtrl::onSelectButtonClicked()
{
    /**
     * Windows cycles a three-state box from checked to indeterminate to unchecked, and TortoiseGit forces
     * indeterminate on to unchecked (CAddDlg::OnBnClickedSelectall): so a click unchecks everything if anything is
     * checked, and checks everything otherwise.
     */
    m_model->setAllChecked(m_model->checkedCount() == 0);
}

void GitStatusListCtrl::onDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    const GitStatusListEntry& entry = m_model->entry(m_proxy->mapToSource(index).row());
    // TortoiseGit edits conflicts and diffs unversioned files against nothing here; neither is ported yet
    if (!isTracked(entry) || (entry.actions & Action::Unmerged)) {
        return;
    }
    startDiff(entry);
}

QList<GitStatusListEntry> GitStatusListCtrl::selectedEntries() const
{
    QList<GitStatusListEntry> entries;
    const QModelIndexList rows = selectionModel()->selectedRows();
    for (const QModelIndex& row : rows) {
        entries.append(m_model->entry(m_proxy->mapToSource(row).row()));
    }
    return entries;
}

void GitStatusListCtrl::startDiff(const GitStatusListEntry& entry)
{
    // The working tree against HEAD, or against the index before the first commit (CGitStatusListCtrl::StartDiff)
    QStringList arguments{QStringLiteral("difftool"), QStringLiteral("--no-prompt")};
    if (m_isInitialRepo) {
        arguments.append(QStringLiteral("--cached"));
    } else {
        arguments.append({QStringLiteral("-M"), QStringLiteral("HEAD")});
    }
    arguments.append(QStringLiteral("--"));
    if (!entry.origPath.isEmpty()) {
        arguments.append(entry.origPath);
    }
    arguments.append(entry.path);
    if (!QProcess::startDetached(QStringLiteral("git"), arguments, m_workingTree)) {
        QMessageBox::warning(this, i18n("Compare with base"), i18n("Could not start git difftool."));
    }
}

void GitStatusListCtrl::open(const GitStatusListEntry& entry)
{
    // KIO jobs delete themselves when they finish
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(absolutePath(entry)));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    job->start();
}

void GitStatusListCtrl::openWith(const GitStatusListEntry& entry)
{
    // Without a service, the job asks which application to use
    auto *job = new KIO::ApplicationLauncherJob();
    job->setUrls({QUrl::fromLocalFile(absolutePath(entry))});
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    job->start();
}

void GitStatusListCtrl::explore(const GitStatusListEntry& entry) const
{
    KIO::highlightInFileManager({QUrl::fromLocalFile(absolutePath(entry))});
}

QStringList GitStatusListCtrl::pathSpec() const
{
    if (m_pathFilter.isEmpty()) {
        return {};
    }
    return QStringList{QStringLiteral("--")} + m_pathFilter;
}

} // namespace kgt
