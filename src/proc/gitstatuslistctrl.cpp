// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/GitStatusListCtrl.cpp

#include "gitstatuslistctrl.hpp"

#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "git/massivegittask.hpp"
#include "progresscommands/revertprogresscommand.hpp"
#include "utils/runtimeerror.hpp"

#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
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
#include <tuple>
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

/**
 * Sorts the list, keeping the groups of a merge's files in parent order whichever way it is sorted, as the
 * Windows list view keeps its groups.
 */
class GitStatusListProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        const auto *model = qobject_cast<const GitStatusListModel *>(sourceModel());
        if (model && model->hasGroups() && !left.parent().isValid()) {
            return (sortOrder() == Qt::AscendingOrder) == (left.row() < right.row());
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }
};

/**
 * The length of a hash in a group header (CGit::GetShortHASHLength).
 */
constexpr qsizetype shortHashLength = 8;

} // namespace

namespace kgt
{

GitStatusListCtrl::GitStatusListCtrl(QWidget *parent)
    : QTreeView(parent)
    , m_model(new GitStatusListModel(this))
    , m_proxy(new GitStatusListProxy(this))
    , m_emptyString(i18n("File list is empty"))
    , m_busyString(i18n("Please wait..."))
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

void GitStatusListCtrl::setIncludeStaged(bool include)
{
    m_includeStaged = include;
}

void GitStatusListCtrl::setHasCheckboxes(bool hasCheckboxes)
{
    m_model->setHasCheckboxes(hasCheckboxes);
}

void GitStatusListCtrl::setContextMenus(ContextMenus menus)
{
    m_contextMenus = menus;
}

void GitStatusListCtrl::setCurrentVersion(const QString& revision, const QStringList& parents)
{
    m_currentVersion = revision;
    m_parentVersions = parents;
    // PrepareGroups' header for each parent: "Diff with parent 1: <short hash>"
    QStringList headers;
    if (parents.size() > 1) {
        for (qsizetype i = 0; i < parents.size(); ++i) {
            headers.append(i18n("Diff with parent %1", i + 1) + QStringLiteral(": ") + parents.at(i).left(shortHashLength));
        }
    }
    m_model->setParentHeaders(headers);
}

const QString& GitStatusListCtrl::currentVersion() const
{
    return m_currentVersion;
}

void GitStatusListCtrl::updateWithGitPathList(const QList<GitStatusListEntry>& entries)
{
    m_statusCall.reset();
    m_numStatCall.reset();
    m_errorMessage.clear();
    m_isBusy = false;
    m_model->setEntries(entries);
    showGroups();
    viewport()->update();
}

void GitStatusListCtrl::clear()
{
    updateWithGitPathList({});
}

void GitStatusListCtrl::setBusy(bool busy)
{
    m_isBusy = busy;
    viewport()->update();
}

void GitStatusListCtrl::setBusyString(const QString& text)
{
    m_busyString = text;
    viewport()->update();
}

void GitStatusListCtrl::reload()
{
    if (!m_currentVersion.isEmpty()) {
        return;
    }
    Q_ASSERT(!m_workingTree.isEmpty());
    if (!m_hasDirectFiles) {
        // CGitStatusListCtrl::GetStatus remembers the files the user picked, so they can be checked
        QStringList directFiles;
        const QDir root(m_workingTree);
        std::ranges::copy_if(m_pathFilter, std::back_inserter(directFiles), [&root](const QString& path) {
            return !QFileInfo(root.filePath(path)).isDir();
        });
        m_model->setDirectFiles(directFiles);
        m_hasDirectFiles = true;
    }
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
    return m_isBusy || m_statusCall || m_numStatCall || m_revertCommand || m_resolveCall;
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
    addConflictEntries(menu, static_cast<int>(selected.size()));
    QList<GitStatusListEntry> tracked;
    std::copy_if(selected.cbegin(), selected.cend(), std::back_inserter(tracked), isTracked);
    if (!tracked.isEmpty()) {
        QAction *compare = menu.addAction(QIcon::fromTheme(QStringLiteral("vcs-diff")), i18n("Compare with b&ase"), this, [this, tracked] {
            for (const GitStatusListEntry& entry : tracked) {
                startDiff(entry);
            }
        });
        // Edit conflicts stays the default for a single conflicted file
        if (!menu.defaultAction()) {
            menu.setDefaultAction(compare);
        }
        menu.addSeparator();
    }
    // The entry the menu was opened on decides, as TortoiseGit's wcStatus does
    const GitStatusListEntry *focused = focusedEntry();
    if (focused && isTracked(*focused) && m_currentVersion.isEmpty() && m_contextMenus.testFlag(ContextMenu::Revert) && !m_revertCommand) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), i18n("Re&vert..."), this, [this] {
            // After the menu has closed, so the confirmation isn't nested in it
            QMetaObject::invokeMethod(this, &GitStatusListCtrl::revertSelected, Qt::QueuedConnection);
        });
    }
    // Open and Explore act on the working tree's file, which is not what a commit's list shows
    if (selected.size() == 1 && m_currentVersion.isEmpty()) {
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

void GitStatusListCtrl::setEmptyString(const QString& text)
{
    m_emptyString = text;
    viewport()->update();
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
        message = m_busyString;
    } else if (!m_errorMessage.isEmpty()) {
        message = m_errorMessage;
    } else {
        message = m_emptyString;
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
    if (m_includeStaged) {
        // The status covered the whole working tree: keep what the filter selects, and everything staged
        m_pendingStatus.removeIf([this](const GitStatusEntry& entry) {
            return !isInPathFilter(entry) && entry.index == FileState::Unmodified;
        });
    }
    m_isInitialRepo = output.startsWith(initialBranchHeader);
    if (m_isInitialRepo) {
        // Nothing to count lines against; TortoiseGit lists an initial commit's files without counts too
        finishLoad({});
        return;
    }
    const QStringList numStatPaths = m_includeStaged ? QStringList() : m_pathFilter;
    m_numStatCall = Git(m_workingTree).runAsync(GitNumStat::arguments(QStringLiteral("HEAD"), numStatPaths), RunMode::ReadOnly);
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
    const int total = m_model->listedCount();
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

void GitStatusListCtrl::showGroups()
{
    // Group headers span the row, and every group is open
    if (!m_model->hasGroups()) {
        return;
    }
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        setFirstColumnSpanned(row, {}, true);
    }
    expandAll();
}

void GitStatusListCtrl::onDoubleClicked(const QModelIndex& index)
{
    const GitStatusListEntry *entry = m_model->entry(m_proxy->mapToSource(index));
    // TortoiseGit diffs unversioned files against nothing here; that is not ported yet
    if (!entry || !isTracked(*entry)) {
        return;
    }
    if (entry->actions & Action::Unmerged) {
        editConflict(*entry);
        return;
    }
    startDiff(*entry);
}

void GitStatusListCtrl::addConflictEntries(QMenu& menu, int selectedCount)
{
    // The entry the menu was opened on decides, as TortoiseGit's wcStatus does
    const GitStatusListEntry *focused = focusedEntry();
    if (!focused || !(focused->actions & Action::Unmerged) || !m_currentVersion.isEmpty()) {
        return;
    }
    if (selectedCount == 1 && m_contextMenus.testFlag(ContextMenu::Conflict)) {
        const GitStatusListEntry entry = *focused;
        QAction *edit = menu.addAction(QIcon::fromTheme(QStringLiteral("merge")), i18n("&Edit conflicts"), this, [this, entry] {
            editConflict(entry);
        });
        menu.setDefaultAction(edit);
    }
    // TortoiseGit's "Resolve conflict using 'theirs'/'mine'" entries are not ported yet
    if (m_contextMenus.testFlag(ContextMenu::Resolve) && !m_resolveCall) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), i18n("Resolved"), this, [this] {
            // After the menu has closed, so the confirmation isn't nested in it
            QMetaObject::invokeMethod(this, &GitStatusListCtrl::resolveSelected, Qt::QueuedConnection);
        });
    }
    if (m_contextMenus & (ContextMenu::Conflict | ContextMenu::Resolve)) {
        menu.addSeparator();
    }
}

void GitStatusListCtrl::editConflict(const GitStatusListEntry& entry)
{
    /**
     * TortoiseGit's CAppUtils::ConflictEdit opens TortoiseMerge; here git mergetool runs the user's merge tool,
     * detached, and marks the file resolved itself when the tool reports success. F5 shows the result.
     */
    const QStringList arguments{QStringLiteral("mergetool"), QStringLiteral("--no-prompt"), QStringLiteral("--"), entry.path};
    if (!QProcess::startDetached(QStringLiteral("git"), arguments, m_workingTree)) {
        QMessageBox::warning(this, i18n("Edit conflicts"), i18n("Could not start git mergetool."));
    }
}

void GitStatusListCtrl::resolveSelected()
{
    if (m_resolveCall) {
        return;
    }
    QStringList paths;
    for (const GitStatusListEntry& entry : selectedEntries()) {
        if (entry.actions & Action::Unmerged) {
            paths.append(entry.path);
        }
    }
    if (paths.isEmpty()) {
        return;
    }
    const QString question = i18n("Are you sure you want to mark the conflicted file(s) as resolved?");
    if (QMessageBox::question(this, i18n("KortoiseGit"), question, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    // ResolveProgressCommand: git add -f on the conflicted files (CAppUtils::RemoveTempMergeFile has nothing to remove here)
    MassiveGitTask addTask({QStringLiteral("add"), QStringLiteral("-f")});
    addTask.setPaths(paths);
    m_resolveCall = addTask.runAsync(Git(m_workingTree));
    connect(m_resolveCall.get(), &GitCall::finished, this, &GitStatusListCtrl::onResolveFinished);
    setCursor(Qt::BusyCursor);
}

void GitStatusListCtrl::onResolveFinished()
{
    const GitCall *call = takeFinished(m_resolveCall);
    unsetCursor();
    try {
        std::ignore = call->output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Window-modal without a nested event loop
        auto *box = new QMessageBox(QMessageBox::Critical, i18n("KortoiseGit"), e.error().message, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
    // TortoiseGit marks the entries modified, or refreshes the parent after an error; reloading does both
    reload();
    Q_EMIT resolved();
}

const GitStatusListEntry *GitStatusListCtrl::focusedEntry() const
{
    // CGitStatusListCtrl::OnContextMenuList's selIndex: the selection mark, or else the first selected entry
    const QModelIndex current = currentIndex();
    if (current.isValid() && selectionModel()->isRowSelected(current.row(), current.parent())) {
        if (const GitStatusListEntry *entry = m_model->entry(m_proxy->mapToSource(current))) {
            return entry;
        }
    }
    const QModelIndexList rows = selectionModel()->selectedRows();
    return rows.isEmpty() ? nullptr : m_model->entry(m_proxy->mapToSource(rows.constFirst()));
}

QList<GitStatusListEntry> GitStatusListCtrl::selectedEntries() const
{
    QList<GitStatusListEntry> entries;
    const QModelIndexList rows = selectionModel()->selectedRows();
    for (const QModelIndex& row : rows) {
        if (const GitStatusListEntry *entry = m_model->entry(m_proxy->mapToSource(row))) {
            entries.append(*entry);
        }
    }
    return entries;
}

void GitStatusListCtrl::startDiff(const GitStatusListEntry& entry)
{
    // The working tree against HEAD, or against the index before the first commit (CGitStatusListCtrl::StartDiff)
    QStringList arguments{QStringLiteral("difftool"), QStringLiteral("--no-prompt")};
    if (!m_currentVersion.isEmpty()) {
        arguments.append(revisionDiffArguments(entry.parentNo));
        if (arguments.size() == 2) {
            QMessageBox::warning(this, i18n("Compare with base"), i18n("Could not start git difftool."));
            return;
        }
    } else if (m_isInitialRepo) {
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

QStringList GitStatusListCtrl::revisionDiffArguments(int parentNo) const
{
    // The commit against the parent the change is against (m_CurrentVersion^N in CGitStatusListCtrl::StartDiff)
    QString parent = m_parentVersions.value(parentNo);
    if (parent.isEmpty()) {
        // A root commit is compared with the empty tree, whose hash depends on the repository's hash algorithm
        try {
            parent = QString::fromLatin1(
                Git(m_workingTree)
                    .run({QStringLiteral("hash-object"), QStringLiteral("-t"), QStringLiteral("tree"), QStringLiteral("--stdin")}, RunMode::ReadOnly)
                    .trimmed());
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
            return {};
        }
    }
    return {QStringLiteral("-M"), parent, m_currentVersion};
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

void GitStatusListCtrl::revertSelected()
{
    const QList<GitStatusListEntry> selected = selectedEntries();
    if (selected.isEmpty() || m_revertCommand) {
        return;
    }
    // If at least one item is a modified file, ask first (IDGITLC_REVERT)
    const bool confirm = std::ranges::any_of(selected, [this](const GitStatusListEntry& entry) {
        return (entry.actions & Action::Modified) && !QFileInfo(absolutePath(entry)).isDir();
    });
    if (confirm) {
        const QString question = i18n("Are you sure you want to revert %1 item(s)? You will lose ALL changes since the last update!", selected.size());
        if (QMessageBox::question(this, i18n("KortoiseGit"), question, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }
    // TortoiseGit reverts an amended commit's files to HEAD~1; the commit dialog has no amend yet
    m_revertCommand = std::make_unique<RevertProgressCommand>();
    m_revertCommand->setPathList(selected);
    connect(m_revertCommand.get(), &RevertProgressCommand::finished, this, &GitStatusListCtrl::onRevertFinished);
    setCursor(Qt::BusyCursor);
    Q_EMIT checkedCountChanged();
    m_revertCommand->run(m_workingTree);
}

void GitStatusListCtrl::onRevertFinished()
{
    // Deleted from the event loop: it is still emitting, maybe from inside run()
    RevertProgressCommand *command = m_revertCommand.release();
    command->deleteLater();
    unsetCursor();
    if (const std::optional<Error>& error = command->error()) {
        // Window-modal without a nested event loop
        auto *box = new QMessageBox(QMessageBox::Critical, i18n("KortoiseGit"), error->message, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
    // TortoiseGit drops the reverted entries or refreshes the parent dialog; reloading does both
    reload();
    Q_EMIT reverted();
}

QStringList GitStatusListCtrl::pathSpec() const
{
    if (m_pathFilter.isEmpty() || m_includeStaged) {
        return {};
    }
    return QStringList{QStringLiteral("--")} + m_pathFilter;
}

bool GitStatusListCtrl::isInPathFilter(const GitStatusEntry& entry) const
{
    if (m_pathFilter.isEmpty()) {
        return true;
    }
    return std::ranges::any_of(m_pathFilter, [&entry](const QString& filter) {
        const auto isUnder = [&filter](const QString& path) {
            return !path.isEmpty() && (path == filter || path.startsWith(filter + QLatin1Char('/')));
        };
        return isUnder(entry.path) || isUnder(entry.origPath);
    });
}

} // namespace kgt
