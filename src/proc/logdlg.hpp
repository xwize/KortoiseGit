// SPDX-FileCopyrightText: 2003-2009, 2015 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/LogDlg.h (CLogDlg): the log dialog. This is its first cut: the commit
 * list, the selected commit's message and its changed files. The graph, filters, date range and action menus come
 * later.
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QTextEdit;

namespace kgt
{

class GitLogList;
class GitStatusListCtrl;
struct GitRevLoglist;

/**
 * The log dialog: the history of HEAD, limited to the paths it was opened for when they are a file or a
 * subfolder. Selecting one commit shows its full message and the files it changed against each of its parents;
 * double-clicking a file compares it in git difftool.
 */
class LogDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Opens the dialog and starts loading the log.
     * @param paths The absolute paths the log was asked for, all in one repository.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit LogDlg(const QStringList& paths, QWidget *parent = nullptr);
    ~LogDlg() override;

    LogDlg(const LogDlg&) = delete;
    LogDlg& operator=(const LogDlg&) = delete;
    LogDlg(LogDlg&&) = delete;
    LogDlg& operator=(LogDlg&&) = delete;

    /**
     * @returns The directory git runs in: the working tree root, or the bare repository.
     */
    [[nodiscard]] const QString& workingTree() const;

    /**
     * @returns The paths the log is limited to, relative to the working tree root; empty for the whole history.
     */
    [[nodiscard]] const QStringList& pathFilter() const;

    /**
     * @returns The commit list.
     */
    [[nodiscard]] GitLogList *logList() const;

    /**
     * @returns The selected commit's changed files.
     */
    [[nodiscard]] GitStatusListCtrl *changedFileList() const;

    /**
     * @returns The text of the message pane.
     */
    [[nodiscard]] QString messageText() const;

private:
    void createWidgets();
    void onSelectedRevChanged();
    void fillLogMessage(const GitRevLoglist& rev);
    void onFilesLoaded(int row);
    void showFiles(const GitRevLoglist& rev);

    QString m_workingTree;
    QStringList m_pathFilter;
    GitLogList *m_logList = nullptr;
    QTextEdit *m_msgView = nullptr;
    GitStatusListCtrl *m_changedFileList = nullptr;
};

} // namespace kgt
