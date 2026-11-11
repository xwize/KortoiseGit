// SPDX-FileCopyrightText: 2007-2010, 2015, 2020 TortoiseSVN
// SPDX-FileCopyrightText: 2016, 2023 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SettingsPropPage.h (ISettingsPropPage), with the parts of MFC's
 * CPropertyPage the pages rely on: OnApply and SetModified.
 */

#pragma once

#include <QWidget>

namespace kgt
{

/**
 * Base class of the Settings dialog's pages.
 */
class SettingsPropPage : public QWidget
{
    Q_OBJECT

public:
    /**
     * @param parent Parent widget.
     */
    explicit SettingsPropPage(QWidget *parent = nullptr);

    /**
     * CPropertyPage::OnApply: saves the page's changes, on OK and on Apply. A page with nothing to save keeps this.
     * @returns False if saving failed or the user stopped it, which keeps the dialog open.
     */
    [[nodiscard]] virtual bool apply();

    /**
     * @returns Whether the page has changes that Apply would save.
     */
    [[nodiscard]] bool isModified() const;

Q_SIGNALS:
    /**
     * The page got or lost changes to save; the dialog enables Apply by it.
     * @param modified Whether it has changes now.
     */
    void modifiedChanged(bool modified);

protected:
    /**
     * CPropertyPage::SetModified.
     * @param modified Whether the page has changes to save.
     */
    void setModified(bool modified = true);

private:
    bool m_modified = false;
};

} // namespace kgt
