// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Stands in for TortoiseGit src/TortoiseProc/LogDlg.h (CLogDlg) until the log dialog is ported.
 */

#pragma once

#include <QDialog>
#include <QStringList>

class QListView;

namespace kgt
{

/**
 * Placeholder for the log dialog: lists the paths it was opened for, so the plugin-to-app launch can be checked.
 * The log dialog card replaces it with the real dialog.
 */
class LogDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param paths The paths the log was requested for.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit LogDlg(const QStringList& paths, QWidget *parent = nullptr);

    /**
     * @returns The paths shown, in the order given.
     */
    [[nodiscard]] QStringList paths() const;

private:
    QListView *m_pathList;
};

} // namespace kgt
