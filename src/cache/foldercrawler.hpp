// SPDX-FileCopyrightText: 2005-2008, 2011, 2014 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2014, 2016-2019, 2021, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TGitCache/FolderCrawler.h.
 */

#pragma once

#include "directorywatcher.hpp"
#include "git/gitcall.hpp"
#include "git/gitstatus.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>

namespace kgt
{

class GitStatusCache;

/**
 * What one re-check of a working tree found.
 */
struct CrawlResult {
    QString root; // The working tree's top folder
    QStringList scope; // The absolute paths git was asked about; the root alone for a whole-tree crawl
    QList<GitStatusEntry> entries; // What git status said about them
    std::optional<QStringList> trackedDirs; // Folders and submodules in HEAD, relative to the root; whole-tree crawls only
};

/**
 * Queues changed paths and re-checks them in the background, one git command at a time.
 *
 * CFolderCrawler asks an in-process library about each path on a worker thread. Here each question is a git process,
 * so paths queued for one working tree go to a single git status, and everything else stays on the event loop, which
 * needs no locks: git's child processes are what runs in the background.
 */
class FolderCrawler : public QObject
{
    Q_OBJECT

public:
    /**
     * @param cache The cache that gets the results, and says which paths are blocked.
     * @param parent Owner.
     */
    explicit FolderCrawler(GitStatusCache& cache, QObject *parent = nullptr);

    /**
     * Ported from CFolderCrawler::AddPathForUpdate.
     * @param changed A changed file or folder, and its working tree.
     */
    void addPathForUpdate(const TreePath& changed);

    /**
     * Ported from CFolderCrawler::AddDirectoryForUpdate.
     * @param folder The folder to read again with everything below it, and its working tree; the root reads the whole
     *        tree.
     */
    void addFolderForCrawling(const TreePath& folder);

    /**
     * Forgets a working tree that is no longer in the cache.
     * @param root Its top folder.
     */
    void removeTree(const QString& root);

    /**
     * Ported from CFolderCrawler::WakeUp: looks for work, for when a blocked tree is free again.
     */
    void wakeUp();

    /**
     * Parses git ls-tree -r -t -z HEAD.
     * @param output What it printed.
     * @returns The folders and submodules, relative to the working tree's root.
     */
    [[nodiscard]] static QStringList parseTrackedDirs(const QByteArray& output);

private:
    /**
     * What is queued for one working tree.
     */
    struct Pending {
        bool wholeTree = false; // Everything needs reading: the paths don't matter
        QSet<QString> paths;
    };

    void crawlNext();
    void startJob(const QString& root, const Pending& pending);
    void onStatusFinished();
    void onTrackedDirsFinished();
    void releaseCall();
    void finishJob();
    void abandonJob();

    GitStatusCache *m_cache;
    QHash<QString, Pending> m_pending;
    std::unique_ptr<GitCall> m_call; // The git command running, if any
    CrawlResult m_result; // What the running job has found so far
    QElapsedTimer m_jobTime;
    QTimer m_wakeTimer; // Gathers events, and spaces the git commands apart so that a burst can't flood the system
    QTimer m_watchdog; // Gives up on a git that doesn't finish
};

} // namespace kgt
