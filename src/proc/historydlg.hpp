// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2021, 2023-2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/HistoryDlg.h (CHistoryDlg) and its IDD_HISTORYDLG resource: picks one or more
 * entries of a history, such as the commit dialog's recent messages.
 */

#pragma once

#include <QDialog>
#include <QString>

class QListWidget;

namespace kgt
{

class RegHistory;

/**
 * The "Log History" dialog: the entries of a history, each on one line. OK takes the selected entries, a double-click
 * takes the one clicked, and Delete removes the current entry from the history for good.
 */
class HistoryDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param history The history shown; Delete removes entries from it and saves it. It must outlive the dialog.
     * @param parent Parent widget.
     */
    explicit HistoryDlg(RegHistory& history, QWidget *parent = nullptr);

    /**
     * @returns The chosen entries, separated by blank lines, once the dialog is accepted (GetSelectedText).
     */
    [[nodiscard]] const QString& selectedText() const;

    /**
     * @returns The list of entries.
     */
    [[nodiscard]] QListWidget *list() const;

    void accept() override;
    void done(int result) override;

private:
    /**
     * Ported from CHistoryDlg::OnLbnDblclkHistorylist.
     */
    void onDoubleClicked();

    /**
     * Ported from CHistoryDlg::PreTranslateMessage's VK_DELETE.
     */
    void onDelete();

    RegHistory *m_history; // Not owned
    QListWidget *m_list;
    QString m_selectedText;
};

} // namespace kgt
