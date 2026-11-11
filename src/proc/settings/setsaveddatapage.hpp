// SPDX-FileCopyrightText: 2003-2008, 2014 TortoiseSVN
// SPDX-FileCopyrightText: 2012-2021, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SetSavedDataPage.h (CSetSavedDataPage).
 */

#pragma once

#include "settings/settingsproppage.hpp"

class QPushButton;

namespace kgt
{

/**
 * The Settings dialog's "Saved Data" page: how much we have remembered, with a Clear button for each kind. Of
 * TortoiseGit's rows it has the ones for data we keep, all of it in KConfig: the histories of the combo boxes, the
 * dialogs' sizes and the answers given with "Don't show this message again". Clearing happens at once, not on Apply.
 */
class SetSavedDataPage : public SettingsPropPage
{
    Q_OBJECT

public:
    /**
     * Counts what is stored.
     * @param parent Parent widget.
     */
    explicit SetSavedDataPage(QWidget *parent = nullptr);

    /**
     * @returns How many history entries are stored, in every history of the "History" group.
     */
    [[nodiscard]] static qsizetype historyCount();

    /**
     * @returns How many dialogs have a stored size.
     */
    [[nodiscard]] static qsizetype resizableCount();

    /**
     * @returns How many "Don't show this message again" answers are stored.
     */
    [[nodiscard]] static qsizetype storedDecisionCount();

private:
    /**
     * Ported from CSetSavedDataPage::OnBnClickedUrlhistclear.
     */
    void onUrlHistClear();

    /**
     * Ported from CSetSavedDataPage::OnBnClickedResizablehistclear.
     */
    void onResizableHistClear();

    /**
     * Ported from CSetSavedDataPage::OnBnClickedStoreddecisionsclear.
     */
    void onStoredDecisionsClear();

    QPushButton *m_urlHistClear;
    QPushButton *m_resizableHistClear;
    QPushButton *m_storedDecisionsClear;
};

} // namespace kgt
