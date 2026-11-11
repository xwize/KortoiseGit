// SPDX-FileCopyrightText: 2003-2008, 2014 TortoiseSVN
// SPDX-FileCopyrightText: 2012-2021, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SetSavedDataPage.cpp and its IDD_SETTINGSSAVEDDATA resource,
 * without the rows for what we don't keep: log messages, authentication data, temporary files, the log cache and
 * the action log.
 */

#include "settings/setsaveddatapage.hpp"

#include "apputils.hpp"
#include "historycombo.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>

namespace
{

/**
 * Ported from RecursivelyCount in SetSavedDataPage.cpp.
 * @param group A history, or a group of histories.
 * @returns How many entries its lists and those of its subgroups hold.
 */
qsizetype recursivelyCount(const KConfigGroup& group)
{
    qsizetype count = 0;
    for (const QString& key : group.keyList()) {
        // A list is one history; a single value, such as the remote last pulled from, counts once
        count += group.readEntry(key, QStringList()).size();
    }
    for (const QString& name : group.groupList()) {
        count += recursivelyCount(group.group(name));
    }
    return count;
}

/**
 * @returns Where KMessageBox keeps the "Don't show this message again" answers.
 */
KConfigGroup storedDecisions()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("Notification Messages"));
}

} // namespace

namespace kgt
{

SetSavedDataPage::SetSavedDataPage(QWidget *parent)
    : SettingsPropPage(parent)
    , m_urlHistClear(new QPushButton(i18n("Clear"), this))
    , m_resizableHistClear(new QPushButton(i18n("Clear"), this))
    , m_storedDecisionsClear(new QPushButton(i18n("Clear"), this))
{
    auto *layout = new QGridLayout(this);
    const auto addRow = [this, layout](int row, const QString& text, QPushButton *clear, qsizetype count, const QString& toolTip) {
        layout->addWidget(new QLabel(text, this), row, 0);
        layout->addWidget(clear, row, 1);
        clear->setEnabled(count > 0);
        if (count > 0) {
            clear->setToolTip(toolTip);
        }
    };
    // TortoiseGit counts the URLs and the working trees they belong to; our histories aren't all kept by working
    // tree, and the branch names typed live beside the URLs, so every entry is counted
    const qsizetype histories = historyCount();
    addRow(0, i18n("URL history"), m_urlHistClear, histories, i18np("Clears %1 stored history entry", "Clears %1 stored history entries", histories));
    const qsizetype dialogs = resizableCount();
    addRow(1,
           i18n("Dialog sizes and positions"),
           m_resizableHistClear,
           dialogs,
           i18np("Clears the stored sizes and positions of %1 dialog", "Clears the stored sizes and positions of %1 dialogs", dialogs));
    const qsizetype decisions = storedDecisionCount();
    addRow(2, i18n("Stored decisions"), m_storedDecisionsClear, decisions, i18np("Clears %1 stored decision", "Clears %1 stored decisions", decisions));
    layout->setColumnStretch(0, 1);
    layout->setRowStretch(3, 1);

    connect(m_urlHistClear, &QPushButton::clicked, this, &SetSavedDataPage::onUrlHistClear);
    connect(m_resizableHistClear, &QPushButton::clicked, this, &SetSavedDataPage::onResizableHistClear);
    connect(m_storedDecisionsClear, &QPushButton::clicked, this, &SetSavedDataPage::onStoredDecisionsClear);
}

qsizetype SetSavedDataPage::historyCount()
{
    return recursivelyCount(HistoryCombo::allHistories());
}

qsizetype SetSavedDataPage::resizableCount()
{
    return AppUtils::allResizableStates().groupList().size();
}

qsizetype SetSavedDataPage::storedDecisionCount()
{
    return storedDecisions().keyList().size();
}

void SetSavedDataPage::onUrlHistClear()
{
    KConfigGroup histories = HistoryCombo::allHistories();
    histories.deleteGroup();
    histories.sync();
    m_urlHistClear->setEnabled(false);
    m_urlHistClear->setToolTip(QString());
}

void SetSavedDataPage::onResizableHistClear()
{
    KConfigGroup states = AppUtils::allResizableStates();
    states.deleteGroup();
    states.sync();
    m_resizableHistClear->setEnabled(false);
    m_resizableHistClear->setToolTip(QString());
}

void SetSavedDataPage::onStoredDecisionsClear()
{
    // TortoiseGit removes its registry values one by one; KMessageBox keeps them all in one group
    KMessageBox::enableAllMessages();
    m_storedDecisionsClear->setEnabled(false);
    m_storedDecisionsClear->setToolTip(QString());
}

} // namespace kgt
