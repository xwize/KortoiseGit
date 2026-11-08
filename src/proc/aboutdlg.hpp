// SPDX-FileCopyrightText: 2003-2006 Stefan Kueng
// SPDX-FileCopyrightText: 2023, 2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/AboutDlg.h (CAboutDlg): the About dialog.
 */

#pragma once

#include <QDialog>

namespace kgt
{

/**
 * The "About KortoiseGit" dialog: the logo, the "based on" line, the contributors, the website link and a Version
 * Information box whose text can be selected, for pasting into bug reports. TortoiseGit's Check For Updates button,
 * donate link, hotfix.ini handling and water effect on the logo are left out.
 */
class AboutDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit AboutDlg(QWidget *parent = nullptr);
};

} // namespace kgt
