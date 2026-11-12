// SPDX-FileCopyrightText: 2009-2013, 2016-2019, 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/RefLogDlg.h (CRefLogDlg): the RefLog dialog, which is also Stash List.
 */

#pragma once

#include "git/git.hpp"
#include "git/gitrevloglist.hpp"

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QString>

#include <memory>

class QComboBox;
class QPushButton;

namespace kgt
{

class RefLogList;

/**
 * What OnFindDialogMessage searches for.
 */
struct RefLogFind {
    QString text; // Looked for in each entry's ref, action, hash, subject and body
    Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive; // Whether case matters
    qsizetype start = 0; // The row to start at; past the end means the first
};

/**
 * Shows the reflog of a ref chosen from all the repository's. All of git runs without blocking.
 */
class RefLogDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param workingTree The working tree's root or the bare repository.
     * @param parent Parent widget.
     */
    explicit RefLogDlg(const QString& workingTree, QWidget *parent = nullptr);
    ~RefLogDlg() override;

    RefLogDlg(const RefLogDlg&) = delete;
    RefLogDlg& operator=(const RefLogDlg&) = delete;
    RefLogDlg(RefLogDlg&&) = delete;
    RefLogDlg& operator=(RefLogDlg&&) = delete;

    /**
     * @param ref The ref to start on (m_CurrentBranch), or an empty string for HEAD. Takes effect with refresh().
     */
    void setCurrentBranch(const QString& ref);

    /**
     * Ported from CRefLogDlg::Refresh: lists the refs again, selects the current one (HEAD if it is gone) and loads
     * its reflog.
     */
    void refresh();

    /**
     * The search of CRefLogDlg::OnFindDialogMessage: starts at @p start and wraps round.
     * @param entries The entries.
     * @param find What to look for, and from where.
     * @returns The first matching row, or -1.
     */
    [[nodiscard]] static qsizetype findEntry(const QList<GitRevLoglist>& entries, const RefLogFind& find);

    /**
     * Saves the window size, and keeps the dialog open while git is changing the repository.
     * @param result The dialog's result.
     */
    void done(int result) override;

    /**
     * @returns The commit of the entry selected when OK was clicked (m_SelectedHash), or an empty string if none or
     *          several were.
     */
    [[nodiscard]] const QString& selectedHash() const;

private:
    /**
     * Ported from CRefLogDlg::OnCbnSelchangeRef: loads the chosen ref's reflog.
     */
    void onCbnSelchangeRef();

    /**
     * The end of OnCbnSelchangeRef.
     * @param entries The reflog, newest first.
     */
    void showEntries(const QList<GitRevLoglist>& entries);

    /**
     * Ported from CRefLogDlg::OnBnClickedClearStash.
     */
    void onBnClickedClearStash();

    /**
     * Ported from CRefLogDlg::OnFind: shows the search window.
     */
    void onFind();

    /**
     * The FR_FINDNEXT branch of OnFindDialogMessage.
     * @param text What to look for.
     * @param sensitivity Whether case matters.
     */
    void findNext(const QString& text, Qt::CaseSensitivity sensitivity);

    Git m_git;
    QString m_currentBranch;
    QComboBox *m_chooseRef;
    RefLogList *m_refList;
    QPushButton *m_clearStash;
    QPushButton *m_okButton = nullptr;
    QPointer<QDialog> m_findDialog;
    qsizetype m_searchLine = 0; // m_nSearchLine: where the next search starts
    bool m_clearing = false; // git stash clear is running
    QString m_selectedHash; // CRefLogDlg::m_SelectedHash
    std::unique_ptr<GitCall> m_refsCall; // Listing the refs
    std::unique_ptr<GitCall> m_logCall; // Reading the reflog
    std::unique_ptr<GitCall> m_clearCall; // git stash clear
};

} // namespace kgt
