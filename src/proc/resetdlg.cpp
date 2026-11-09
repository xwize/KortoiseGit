// SPDX-FileCopyrightText: 2008-2017, 2019-2020, 2023-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ResetDlg.cpp and its IDD_RESET resource.
 */

#include "resetdlg.hpp"

#include "chooseversion.hpp"
#include "git/git.hpp"
#include "git/gitadmindir.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KWindowConfig>

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWindow>

namespace
{

constexpr int initialWidth = 520;

/**
 * @returns Where EnableSaveRestore(L"ResetDlg") keeps the window size.
 */
KConfigGroup windowSettings()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("ResetDlg"));
}

} // namespace

namespace kgt
{

ResetDlg::ResetDlg(const Git& git, const QString& initialRefName, QWidget *parent)
    : QDialog(parent)
    , m_chooseVersion(new ChooseVersion(git, i18n("Reset active branch"), this))
    , m_soft(new QRadioButton(i18n("&Soft: Leave working tree and index untouched"), this))
    , m_mixed(new QRadioButton(i18n("&Mixed: Leave working tree untouched, reset index"), this))
    , m_hard(new QRadioButton(i18n("&Hard: Reset working tree and index (discard all local changes)"), this))
{
    setWindowTitle(i18nc("@title:window", "Reset"));

    auto *layout = new QVBoxLayout(this);

    auto *currentBranchRow = new QHBoxLayout;
    currentBranchRow->addWidget(new QLabel(i18n("Current branch:"), this));
    auto *currentBranch = new QLineEdit(this);
    currentBranch->setReadOnly(true);
    currentBranch->setFrame(false);
    currentBranchRow->addWidget(currentBranch, 1);
    layout->addLayout(currentBranchRow);

    layout->addWidget(m_chooseVersion);

    auto *typeGroup = new QGroupBox(i18n("Reset Type"), this);
    auto *typeLayout = new QVBoxLayout(typeGroup);
    typeLayout->addWidget(m_soft);
    typeLayout->addWidget(m_mixed);
    typeLayout->addWidget(m_hard);
    layout->addWidget(typeGroup);

    // TODO: IDC_SHOW_MODIFIED_FILES, "Show modified files in &working tree", opens CFileDiffDlg, which isn't ported
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ResetDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // OnInitDialog: a bare repository has no index or working tree to reset
    QRadioButton *selectedType = m_mixed;
    if (GitAdminDir::isBareRepo(git.workingDirectory())) {
        selectedType = m_soft;
        m_mixed->setEnabled(false);
        m_hard->setEnabled(false);
    }
    selectedType->setChecked(true);

    // InitChooseVersion disables OK until the branches and tags are in
    QPushButton *okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setEnabled(false);
    connect(m_chooseVersion, &ChooseVersion::loaded, this, [this, currentBranch, okButton] {
        currentBranch->setText(m_chooseVersion->currentBranch());
        okButton->setEnabled(true);
    });
    m_chooseVersion->setDefaultChoose(ChooseVersion::Choice::Branch);
    m_chooseVersion->setInitialRefName(initialRefName);
    m_chooseVersion->init();
    selectedType->setFocus();

    // EnableSaveRestore. KWindowConfig needs the native window, which show() would otherwise create
    resize(initialWidth, sizeHint().height());
    create();
    KWindowConfig::restoreWindowSize(windowHandle(), windowSettings());
    resize(windowHandle()->size());
}

ResetType ResetDlg::resetType() const
{
    if (m_soft->isChecked()) {
        return ResetType::Soft;
    }
    if (m_hard->isChecked()) {
        return ResetType::Hard;
    }
    return ResetType::Mixed;
}

const QString& ResetDlg::resetToVersion() const
{
    return m_resetToVersion;
}

ChooseVersion *ResetDlg::chooseVersion() const
{
    return m_chooseVersion;
}

void ResetDlg::accept()
{
    m_resetToVersion = m_chooseVersion->versionName();
    QDialog::accept();
}

void ResetDlg::done(int result)
{
    KConfigGroup settings = windowSettings();
    KWindowConfig::saveWindowSize(windowHandle(), settings);
    settings.sync();
    QDialog::done(result);
}

} // namespace kgt
