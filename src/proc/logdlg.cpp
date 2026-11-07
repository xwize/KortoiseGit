// SPDX-FileCopyrightText: 2003-2009, 2015 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/LogDlg.cpp

#include "logdlg.hpp"

#include "gitloglist.hpp"
#include "gitstatuslistctrl.hpp"

#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "git/gitrevloglist.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSplitter>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include <numeric>

namespace
{

using namespace kgt;

constexpr int logListStretch = 5; // How the splitter shares the dialog's height, as TortoiseGit's layout does
constexpr int msgViewStretch = 2;
constexpr int fileListStretch = 3;
constexpr QSize initialSize(900, 700);

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
 * @returns The paths relative to the root, or an empty list when one of them is the root (the whole history).
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
 * @param path The first path given to the dialog.
 * @returns The working tree root holding it, or, for a bare repository, which has none, the path itself.
 */
QString repositoryRoot(const QString& path)
{
    try {
        return Git::workingTreeRoot(path);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // LogCommand accepts a bare repository; git log runs in it without a path filter
        const QFileInfo info(path);
        return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }
}

} // namespace

namespace kgt
{

LogDlg::LogDlg(const QStringList& paths, QWidget *parent)
    : QDialog(parent)
{
    Q_ASSERT(!paths.isEmpty());
    m_workingTree = repositoryRoot(paths.constFirst());
    if (QFileInfo(resolvedPath(paths.constFirst())).absoluteFilePath().startsWith(m_workingTree + QLatin1Char('/'))) {
        m_pathFilter = relativePaths(m_workingTree, paths);
    }
    createWidgets();

    // CAppUtils::SetWindowTitle: the path the log was opened for, or the working tree for several
    const QString titlePath = paths.size() == 1 ? QDir::toNativeSeparators(paths.constFirst()) : m_workingTree;
    setWindowTitle(i18nc("@title:window %1 is a path", "%1 - Log Messages", titlePath));

    m_logList->setWorkingTree(m_workingTree);
    m_logList->setPathFilter(m_pathFilter);
    m_logList->reload();
    m_logList->setFocus();
}

LogDlg::~LogDlg() = default;

const QString& LogDlg::workingTree() const
{
    return m_workingTree;
}

const QStringList& LogDlg::pathFilter() const
{
    return m_pathFilter;
}

GitLogList *LogDlg::logList() const
{
    return m_logList;
}

GitStatusListCtrl *LogDlg::changedFileList() const
{
    return m_changedFileList;
}

QString LogDlg::messageText() const
{
    return m_msgView->toPlainText();
}

void LogDlg::createWidgets()
{
    auto *layout = new QVBoxLayout(this);
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);

    m_logList = new GitLogList(splitter);
    m_msgView = new QTextEdit(splitter);
    m_msgView->setReadOnly(true);
    m_msgView->setAcceptRichText(false);
    m_changedFileList = new GitStatusListCtrl(splitter);
    m_changedFileList->setHasCheckboxes(false);
    m_changedFileList->setWorkingTree(m_workingTree);
    m_changedFileList->setShowFlags(GitStatusListModel::Show::Versioned, {});

    splitter->setStretchFactor(0, logListStretch);
    splitter->setStretchFactor(1, msgViewStretch);
    splitter->setStretchFactor(2, fileListStretch);
    layout->addWidget(splitter);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(m_logList, &GitLogList::selectedRevChanged, this, &LogDlg::onSelectedRevChanged);
    connect(m_logList->logModel(), &QAbstractItemModel::modelReset, this, &LogDlg::onSelectedRevChanged);
    resize(initialSize);
}

void LogDlg::onSelectedRevChanged()
{
    // CLogDlg::FillLogMessageCtrl
    m_filesCall.reset();
    m_filesHash.clear();
    m_changedFileList->setCurrentVersion({}, {});
    m_changedFileList->clear();
    const int row = m_logList->selectedRow();
    if (row < 0) {
        m_msgView->clear();
        return;
    }
    const GitRevLoglist& rev = m_logList->logModel()->rev(row);
    fillLogMessage(rev);

    m_filesHash = rev.hash;
    m_changedFileList->setBusyString(i18n("Fetching changed files..."));
    m_changedFileList->setBusy(true);
    m_filesCall = Git(m_workingTree).runAsync(GitRevLoglist::filesArguments(rev), RunMode::ReadOnly);
    connect(m_filesCall.get(), &GitCall::finished, this, &LogDlg::onFilesFinished);
}

void LogDlg::fillLogMessage(const GitRevLoglist& rev)
{
    // "SHA-1: <hash>", a blank line, the subject in bold, then the body
    m_msgView->clear();
    QTextCursor cursor(m_msgView->document());
    QTextCharFormat normal;
    normal.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    QTextCharFormat bold = normal;
    bold.setFontWeight(QFont::Bold);
    cursor.insertText(i18n("SHA-1") + QStringLiteral(": ") + rev.hash + QStringLiteral("\n\n"), normal);
    cursor.insertText(rev.subject, bold);
    cursor.insertText(QStringLiteral("\n") + rev.body, normal);
    m_msgView->moveCursor(QTextCursor::Start);
}

void LogDlg::onFilesFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_filesCall.release();
    Q_ASSERT(call);
    call->deleteLater();
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        failFiles();
        return;
    }
    const Result<GitRevFiles> files = GitRevLoglist::parseFiles(output);
    if (!files) {
        failFiles();
        return;
    }
    const int row = m_logList->selectedRow();
    GitLogListModel *model = m_logList->logModel();
    if (row < 0 || model->rev(row).hash != m_filesHash) {
        return;
    }
    const QList<GitStatusListEntry> entries = GitStatusListModel::makeEntries(files->status, files->numStat);
    m_changedFileList->setCurrentVersion(m_filesHash, model->rev(row).parents.value(0));
    m_changedFileList->updateWithGitPathList(entries);
    const TGitPath::Actions actions = std::accumulate(entries.cbegin(), entries.cend(), TGitPath::Actions(), [](TGitPath::Actions all, const GitStatusListEntry& entry) {
        return all | entry.actions;
    });
    model->setActions(row, actions);
}

void LogDlg::failFiles()
{
    // TortoiseGit leaves the list busy with this message (GitRevLoglist::m_IsDiffFiles == 2)
    m_changedFileList->setBusyString(i18n("Could not fetch commit or calculate diff."));
    m_changedFileList->setBusy(true);
}

} // namespace kgt
