// SPDX-FileCopyrightText: 2005-2008, 2011, 2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2014, 2016-2019, 2021, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TGitCache/FolderCrawler.cpp

#include "foldercrawler.hpp"

#include "cachedebug.hpp"
#include "git/gitadmindir.hpp"
#include "gitstatuscache.hpp"
#include "utils/runtimeerror.hpp"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <chrono>
#include <exception>

namespace
{

using namespace std::chrono_literals;

constexpr std::chrono::milliseconds crawlDelay = 100ms; // CFolderCrawler's Sleep(20), longer as each crawl is a process
constexpr std::chrono::seconds jobTimeout = 120s;
constexpr qsizetype maxPathsPerJob = 256; // Beyond this one whole-tree status is cheaper
constexpr qsizetype maxPathspecBytes = 128LL * 1024; // Well under Linux's limit for a command line
constexpr int jobTimeMultiple = 2; // After a job, wait this many times as long as it took: at most a third of a core

/**
 * @param root The working tree's top folder.
 * @returns Whether index.lock exists: GitStatus::IsExistIndexLockFile.
 */
bool hasIndexLock(const QString& root)
{
    return QFile::exists(QDir(kgt::GitAdminDir::worktreeAdminDirPath(root)).filePath(QStringLiteral("index.lock")));
}

} // namespace

namespace kgt
{

FolderCrawler::FolderCrawler(GitStatusCache& cache, QObject *parent)
    : QObject(parent)
    , m_cache(&cache)
{
    m_wakeTimer.setSingleShot(true);
    connect(&m_wakeTimer, &QTimer::timeout, this, &FolderCrawler::crawlNext);
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(jobTimeout);
    connect(&m_watchdog, &QTimer::timeout, this, &FolderCrawler::abandonJob);
}

void FolderCrawler::addPathForUpdate(const TreePath& changed)
{
    // While a tree is blocked its changes needn't queue up: the crawl that ends the block reads all of it
    if (!m_cache->isPathGood(changed.root) || hasIndexLock(changed.root)) {
        return;
    }
    if (changed.path == changed.root) {
        addFolderForCrawling(changed);
        return;
    }
    const QLatin1String gitignore("/.gitignore");
    if (changed.path.endsWith(gitignore)) {
        // CCachedDirectory::GetStatusFromGit: "A change in .gitignore can affect the status of any file in this directory"
        addFolderForCrawling(TreePath{.root = changed.root, .path = changed.path.chopped(gitignore.size())});
        return;
    }
    m_pending.try_emplace(changed.root).first->second.paths.insert(changed.path);
    wakeUp();
}

void FolderCrawler::addFolderForCrawling(const TreePath& folder)
{
    if (!m_cache->isPathGood(folder.root)) {
        return;
    }
    Pending& pending = m_pending.try_emplace(folder.root).first->second;
    if (folder.path == folder.root) {
        pending.wholeTree = true;
    } else {
        pending.paths.insert(folder.path);
    }
    wakeUp();
}

void FolderCrawler::removeTree(const QString& root)
{
    m_pending.remove(root);
}

void FolderCrawler::wakeUp()
{
    if (!m_call && !m_wakeTimer.isActive()) {
        m_wakeTimer.start(crawlDelay);
    }
}

void FolderCrawler::crawlNext()
{
    if (m_call) {
        return;
    }
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (m_cache->isPathGood(it.key())) {
            const QString root = it.key();
            const Pending pending = it.value();
            m_pending.erase(it);
            startJob(root, pending);
            return;
        }
    }
}

void FolderCrawler::startJob(const QString& root, const Pending& pending)
{
    QStringList arguments = GitStatus::arguments(IgnoredFiles::Include);
    const QStringList scope(pending.paths.cbegin(), pending.paths.cend());
    bool wholeTree = pending.wholeTree || scope.size() > maxPathsPerJob;
    if (!wholeTree) {
        const QDir rootDir(root);
        QStringList pathspecs;
        qsizetype bytes = 0;
        for (const QString& path : scope) {
            pathspecs.append(QStringLiteral(":(literal)") + rootDir.relativeFilePath(path));
            bytes += pathspecs.constLast().size() * 3; // The longest a UTF-16 unit gets in UTF-8
        }
        wholeTree = bytes > maxPathspecBytes;
        if (!wholeTree) {
            arguments.append(QStringLiteral("--"));
            arguments.append(pathspecs);
        }
    }
    m_result = CrawlResult{.root = root, .scope = wholeTree ? QStringList{root} : scope, .entries = {}, .trackedDirs = std::nullopt};
    m_jobTime.start();
    m_watchdog.start();
    m_call = std::make_unique<GitCall>(root, arguments, RunMode::ReadOnly);
    connect(m_call.get(), &GitCall::finished, this, &FolderCrawler::onStatusFinished);
}

void FolderCrawler::onStatusFinished()
{
    try {
        m_result.entries = getOrThrow(GitStatus::parse(m_call->output()));
    } catch (const RuntimeError& e) {
        qCDebug(KGT_CACHE) << "No status for" << m_result.root << ":" << e.error().message;
        abandonJob();
        return;
    }
    if (m_result.scope != QStringList{m_result.root}) {
        finishJob();
        return;
    }
    releaseCall();
    // GitFolderStatus::fetch's ls-tree, for the whole tree: only the folders are kept
    m_call = std::make_unique<GitCall>(
        m_result.root,
        QStringList{QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("-t"), QStringLiteral("-z"), QStringLiteral("HEAD")},
        RunMode::ReadOnly);
    connect(m_call.get(), &GitCall::finished, this, &FolderCrawler::onTrackedDirsFinished);
}

void FolderCrawler::onTrackedDirsFinished()
{
    try {
        m_result.trackedDirs = parseTrackedDirs(m_call->output());
    } catch (const RuntimeError& e) {
        switch (e.error().category) {
        case ErrorCategory::Git:
            // A new repository: nothing is committed, so nothing is tracked in HEAD
            m_result.trackedDirs = QStringList{};
            break;
        default:
            throw;
        }
    }
    finishJob();
}

void FolderCrawler::releaseCall()
{
    if (m_call) {
        // deleteLater, as this can run inside the signal the call is emitting
        m_call->disconnect(this);
        m_call.release()->deleteLater();
    }
}

void FolderCrawler::finishJob()
{
    m_watchdog.stop();
    releaseCall();
    const CrawlResult result = std::move(m_result);
    m_result = {};
    m_cache->applyCrawlResult(result);
    m_wakeTimer.start(std::max(crawlDelay, std::chrono::milliseconds(m_jobTime.elapsed() * jobTimeMultiple)));
}

void FolderCrawler::abandonJob()
{
    m_watchdog.stop();
    releaseCall();
    const QString root = m_result.root;
    m_result = {};
    m_cache->crawlFailed(root);
    wakeUp();
}

QStringList FolderCrawler::parseTrackedDirs(const QByteArray& output)
{
    QStringList dirs;
    qsizetype pos = 0;
    while (pos < output.size()) {
        const qsizetype end = output.indexOf('\0', pos);
        if (end < 0) {
            break;
        }
        // "<mode> <type> <hash>\t<path>": a blob's mode starts with 1, a tree's with 0 and a gitlink's with 16
        const QByteArrayView record(output.constData() + pos, end - pos); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): Parsing git's buffer
        if (!record.startsWith("100") && !record.startsWith("120")) {
            const qsizetype tab = record.indexOf('\t');
            if (tab >= 0) {
                dirs.append(QString::fromUtf8(record.sliced(tab + 1)));
            }
        }
        pos = end + 1;
    }
    return dirs;
}

} // namespace kgt
