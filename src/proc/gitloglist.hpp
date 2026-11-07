// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitLogList.h (CGitLogList) and GitLogListBase.h (CGitLogListBase): the
 * commit list of the log dialog. TortoiseGit's owner-drawn CListCtrl becomes a QTreeView over a GitLogListModel
 * with a delegate that draws the ref labels and action icons; its LogThread becomes git log read as it runs.
 */

#pragma once

#include "gitloglistmodel.hpp"

#include "utils/error.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTreeView>

#include <memory>

namespace kgt
{

class GitCall;

/**
 * The log's commit list. The owning dialog sets the working tree and the path filter and calls reload(); the
 * commits appear as git log writes them, so the UI never waits for the whole history.
 */
class GitLogList : public QTreeView
{
    Q_OBJECT

public:
    /**
     * @param parent The parent widget.
     */
    explicit GitLogList(QWidget *parent = nullptr);
    ~GitLogList() override;

    GitLogList(const GitLogList&) = delete;
    GitLogList& operator=(const GitLogList&) = delete;
    GitLogList(GitLogList&&) = delete;
    GitLogList& operator=(GitLogList&&) = delete;

    /**
     * @param root The directory git log runs in: the working tree root, or a bare repository. Takes effect on the
     *             next reload().
     */
    void setWorkingTree(const QString& root);

    /**
     * Limits the log to the commits that touched some paths, as TortoiseGit does for a file or subfolder. Takes
     * effect on the next reload().
     * @param paths Pathspecs relative to the working tree root, or empty for the whole history.
     */
    void setPathFilter(const QStringList& paths);

    /**
     * Empties the list and runs git log in the background, adding the commits page by page as git writes them
     * (CGitLogListBase::FetchLogAsync). Any load still running is abandoned.
     */
    void reload();

    /**
     * @returns Whether git log is running.
     */
    [[nodiscard]] bool isBusy() const;

    /**
     * @returns The model holding the commits, in git's order.
     */
    [[nodiscard]] GitLogListModel *logModel() const;

    /**
     * Asks for a commit's files in the background, ahead of earlier requests (CGitLogListBase's AsyncDiffThread
     * takes the newest request first). filesLoaded() follows; nothing happens if they are already known. The list
     * asks for the commits it draws by itself.
     * @param row A row of the model.
     */
    void requestFiles(int row);

    /**
     * @returns The row of the one selected commit, or -1 when none or several are selected.
     */
    [[nodiscard]] int selectedRow() const;

Q_SIGNALS:
    /**
     * A reload() has finished: every commit is listed.
     */
    void loaded();

    /**
     * A reload() failed; the commits read before the failure stay listed and the empty list shows the message.
     * @param error What went wrong.
     */
    void loadFailed(const kgt::Error& error);

    /**
     * A commit's files have been fetched, or could not be: its filesState says which.
     * @param row The commit's row.
     */
    void filesLoaded(int row);

    /**
     * The selection has changed: a different commit, several or none are selected.
     */
    void selectedRevChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void onOutputAvailable();
    void onLogFinished();
    [[nodiscard]] bool takeRecords();
    void failLoad(const Error& error);
    void requestVisibleFiles();
    void startNextFiles();
    void startFilesCall();
    void onFilesFinished();
    void finishFiles(bool succeeded);

    GitLogListModel *m_model;
    QString m_workingTree;
    QStringList m_pathFilter;
    std::unique_ptr<GitCall> m_logCall;
    QByteArray m_pending; // Output of git log that doesn't make a whole record yet
    QString m_errorMessage; // Shown in the empty list after a failed load
    QList<int> m_filesQueue; // TortoiseGit's m_AsynDiffList: rows waiting for their files, newest last
    std::unique_ptr<GitCall> m_filesCall; // The diff-tree running for m_filesRow
    int m_filesRow = -1; // The row whose files are being fetched, or -1
    QList<GitRevFiles> m_filesDone; // Its diffs fetched so far, one per parent
};

} // namespace kgt
