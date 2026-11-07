// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CreateRepoDlg.h (CCreateRepoDlg): asks whether the new repository is
 * bare.
 */

#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;

namespace kgt
{

/**
 * The "Git Init" dialog: one checkbox, "Make it Bare", checked to begin with when the folder's name ends in .git.
 */
class CreateRepoDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param folder The folder the repository is created in.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit CreateRepoDlg(const QString& folder, QWidget *parent = nullptr);

    /**
     * @returns Whether a bare repository is wanted.
     */
    [[nodiscard]] bool isBare() const;

    /**
     * @param bare Whether a bare repository is wanted.
     */
    void setBare(bool bare);

private:
    QCheckBox *m_bare;
};

} // namespace kgt
