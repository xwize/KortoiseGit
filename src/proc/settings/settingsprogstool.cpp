// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "settings/settingsprogstool.hpp"

#include "utils/runtimeerror.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace
{

/**
 * @param tool A tool as git names it.
 * @returns Its own spelling of its name, for the tools we choose ourselves.
 */
QString toolTitle(const QString& tool)
{
    if (tool == QLatin1String("kdiff3")) {
        return QStringLiteral("KDiff3");
    }
    if (tool == QLatin1String("meld")) {
        return QStringLiteral("Meld");
    }
    return tool;
}

} // namespace

namespace kgt
{

SettingsProgsTool::SettingsProgsTool(const ProgsToolPage& page, QWidget *parent)
    : SettingsPropPage(parent)
    , m_git(QDir::homePath()) // Only the global configuration is touched
    , m_configKey(page.configKey)
    , m_defaultRadio(new QRadioButton(i18n("&Default"), this))
    , m_externalRadio(new QRadioButton(i18n("&External"), this))
    , m_tools(new QComboBox(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(page.description, this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *radioLayout = new QHBoxLayout;
    radioLayout->addWidget(m_defaultRadio);
    radioLayout->addWidget(m_externalRadio);
    radioLayout->addStretch();
    layout->addLayout(radioLayout);
    layout->addWidget(m_tools);

    auto *hint = new QLabel(page.defaultHint, this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch();

    for (const QString& tool : page.tools) {
        const bool installed = !QStandardPaths::findExecutable(tool).isEmpty();
        m_tools->addItem(installed ? toolTitle(tool) : i18nc("%1 is a program", "%1 (not installed)", toolTitle(tool)), tool);
    }
    showTool(m_git.configValueAt(m_configKey, ConfigLevel::Global).value_or(QString()));

    connect(m_defaultRadio, &QRadioButton::clicked, this, &SettingsProgsTool::onChange);
    connect(m_externalRadio, &QRadioButton::clicked, this, &SettingsProgsTool::onChange);
    connect(m_tools, &QComboBox::activated, this, &SettingsProgsTool::onChange);
}

bool SettingsProgsTool::apply()
{
    if (!isModified()) {
        return true;
    }
    const QString chosen = tool();
    try {
        if (!chosen.isEmpty()) {
            m_git.setConfigValue(m_configKey, chosen, ConfigLevel::Global);
        } else if (m_git.configValueAt(m_configKey, ConfigLevel::Global)) {
            m_git.unsetConfigValue(m_configKey, ConfigLevel::Global);
        }
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        const QString message = i18n("Saving config failed (key: \"%1\", value: \"%2\").", m_configKey, chosen);
        KMessageBox::error(this, message + QLatin1Char('\n') + e.error().message, QString(), KMessageBox::Options());
        return false;
    }
    setModified(false);
    return true;
}

QString SettingsProgsTool::tool() const
{
    return m_externalRadio->isChecked() ? m_tools->currentData().toString() : QString();
}

void SettingsProgsTool::setTool(const QString& tool)
{
    showTool(tool);
    setModified();
}

void SettingsProgsTool::showTool(const QString& tool)
{
    m_defaultRadio->setChecked(tool.isEmpty());
    m_externalRadio->setChecked(!tool.isEmpty());
    m_tools->setEnabled(!tool.isEmpty());
    if (tool.isEmpty()) {
        return;
    }
    int index = m_tools->findData(tool);
    if (index < 0) {
        // Another of git's tools, set outside this dialog: kept as it is
        m_tools->addItem(tool, tool);
        index = m_tools->count() - 1;
    }
    m_tools->setCurrentIndex(index);
}

void SettingsProgsTool::onChange()
{
    m_tools->setEnabled(m_externalRadio->isChecked());
    setModified();
}

} // namespace kgt
