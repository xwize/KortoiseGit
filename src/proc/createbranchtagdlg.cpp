// SPDX-FileCopyrightText: 2008-2017, 2019-2021, 2024-2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/CreateBranchTagDlg.cpp and its IDD_NEW_BRANCH_TAG resource. TortoiseGit's
 * registry values NewBranchSwitchTo and PushTag go to KConfig under the same names.
 */

#include "createbranchtagdlg.hpp"

#include "apputils.hpp"
#include "chooseversion.hpp"
#include "git/gitadmindir.hpp"

#include <KConfigGroup>
#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <utility>

namespace
{

constexpr auto newBranchSwitchToKey = "NewBranchSwitchTo";
constexpr auto pushTagKey = "PushTag";
constexpr KMessageBox::Options noNotify = KMessageBox::Options();
// IDC_EDIT_MESSAGE's height in the resource
constexpr int messageLines = 3;

} // namespace

namespace kgt
{

CreateBranchTagDlg::CreateBranchTagDlg(Git git, const CreateBranchTagParams& params, QWidget *parent)
    : QDialog(parent)
    , m_git(std::move(git))
    , m_name(new QLineEdit(params.name, this))
    , m_chooseVersion(new ChooseVersion(m_git, i18n("Base On"), this))
    , m_track(new QCheckBox(i18n("Trac&k"), this))
    , m_force(new QCheckBox(i18n("&Force"), this))
    , m_switch(new QCheckBox(i18n("&Switch to new branch"), this))
    , m_sign(new QCheckBox(i18n("Si&gn"), this))
    , m_push(new QCheckBox(i18n("&Push"), this))
    , m_message(new QPlainTextEdit(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    , m_isTag(params.isTag)
{
    // CAppUtils::SetWindowTitle
    const QString folder = QDir::toNativeSeparators(m_git.workingDirectory());
    setWindowTitle(m_isTag ? i18nc("@title:window %1 is a folder", "%1 - Create Tag", folder)
                           : i18nc("@title:window %1 is a folder", "%1 - Create Branch", folder));

    auto *layout = new QVBoxLayout(this);

    auto *nameGroup = new QGroupBox(i18n("&Name"), this);
    auto *nameLayout = new QHBoxLayout(nameGroup);
    nameLayout->addWidget(new QLabel(m_isTag ? i18n("Tag") : i18n("Branch"), nameGroup));
    nameLayout->addWidget(m_name, 1);
    layout->addWidget(nameGroup);

    layout->addWidget(m_chooseVersion);

    auto *optionGroup = new QGroupBox(i18n("Options"), this);
    auto *optionLayout = new QHBoxLayout(optionGroup);
    optionLayout->addWidget(m_track);
    optionLayout->addWidget(m_force);
    optionLayout->addWidget(m_switch);
    optionLayout->addWidget(m_sign);
    optionLayout->addWidget(m_push);
    optionLayout->addStretch();
    layout->addWidget(optionGroup);

    auto *messageGroup = new QGroupBox(m_isTag ? i18n("&Message") : i18n("Description"), this);
    auto *messageLayout = new QVBoxLayout(messageGroup);
    m_message->setTabChangesFocus(true);
    m_message->setMinimumHeight(m_message->fontMetrics().lineSpacing() * messageLines + 2 * m_message->frameWidth());
    messageLayout->addWidget(m_message);
    layout->addWidget(messageGroup, 1);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &CreateBranchTagDlg::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    // The constructor's defaults, and CAppUtils::CreateBranchTag's m_bSwitch
    m_track->setTristate(true);
    m_track->setCheckState(Qt::PartiallyChecked);
    m_track->setEnabled(false);

    // OnInitDialog: Sign needs a signing key, and Sign and Push only exist for tags
    if (m_isTag) {
        m_sign->setEnabled(!m_git.configValue(QStringLiteral("user.signingkey")).isEmpty());
        m_push->setChecked(AppUtils::generalSettings().readEntry(pushTagKey, false));
    } else {
        m_switch->setChecked(params.switchNewBranch || AppUtils::generalSettings().readEntry(newBranchSwitchToKey, false));
    }
    m_sign->setVisible(m_isTag);
    m_push->setVisible(m_isTag);

    // OnInitDialog: the switch checkbox is only for a create branch dialog with a working tree to switch
    const bool canSwitch = !m_isTag && !GitAdminDir::isBareRepo(m_git.workingDirectory());
    m_switch->setVisible(canSwitch);
    m_switch->setEnabled(canSwitch);

    QRadioButton *head = m_chooseVersion->headRadio();
    head->setVisible(true);
    head->setText(head->text() + QStringLiteral(" (") + QString(m_git.currentBranch()).replace(QLatin1Char('&'), QStringLiteral("&&")) + QLatin1Char(')'));

    m_force->setToolTip(i18n("Force creation of branch/tag - even if one with same name already exists."));
    m_sign->setToolTip(i18n("GPG signs the tag, see manual for details"));
    m_track->setToolTip(
        i18n("If checked --track is passed to git, if unchecked --no-track is passed to git, else neither --track nor --no-track is passed to git (see "
             "help)."));

    // OnEnChangeBranchTag and OnVersionChanged (radio buttons and combo boxes alike)
    connect(m_name, &QLineEdit::textChanged, this, &CreateBranchTagDlg::onNameChanged);
    connect(m_chooseVersion, &ChooseVersion::versionChanged, this, &CreateBranchTagDlg::onVersionChanged);

    // InitChooseVersion disables OK until the branches and tags are in
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_chooseVersion, &ChooseVersion::loaded, this, [this] {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    });
    m_chooseVersion->setDefaultChoose(ChooseVersion::Choice::Head);
    m_chooseVersion->setInitialRefName(params.initialRefName);
    m_chooseVersion->init();

    onBranchSelectionChanged();
    m_name->setFocus();

    resize(520, minimumSizeHint().height());
}

const CreateBranchTagOptions& CreateBranchTagDlg::options() const
{
    return m_options;
}

ChooseVersion *CreateBranchTagDlg::chooseVersion() const
{
    return m_chooseVersion;
}

void CreateBranchTagDlg::accept()
{
    // TortoiseGit lets a blank message through here and CreateBranchTag then drops it, so git tag -s would start an
    // editor, which nobody can answer from a dialog
    if (m_sign->isChecked() && m_message->toPlainText().trimmed().isEmpty()) {
        KMessageBox::error(this, i18n("You must enter a log message for the commit"), QString(), noNotify);
        return;
    }

    const QString name = m_name->text().trimmed();
    m_name->setText(name);
    if (!isNameAccepted(name)) {
        return;
    }

    // TortoiseGit reads the hidden checkbox too, and then fails to switch in a bare repository
    const bool switchToNewBranch = m_switch->isEnabled() && m_switch->isChecked();
    KConfigGroup config = AppUtils::generalSettings();
    if (m_isTag) {
        config.writeEntry(pushTagKey, m_push->isChecked());
    } else if (m_switch->isEnabled()) {
        config.writeEntry(newBranchSwitchToKey, switchToNewBranch);
    }
    config.sync();

    m_options = CreateBranchTagOptions{
        .branchTagName = name,
        .versionName = m_chooseVersion->versionName(),
        .force = m_force->isChecked(),
        .track = m_track->checkState(),
        .switchToNewBranch = switchToNewBranch,
        .message = m_message->toPlainText(),
        .sign = m_sign->isChecked(),
        .push = m_isTag && m_push->isChecked(),
    };
    QDialog::accept();
}

bool CreateBranchTagDlg::isNameAccepted(const QString& name)
{
    // TortoiseGit shows the first three as balloons on the field
    if (!Git::isBranchNameValid(name)) {
        m_name->setFocus();
        KMessageBox::error(this, i18n("Branch/Tag name must not be empty or is invalid."), i18n("Error"), noNotify);
        return false;
    }
    if (!m_force->isChecked() && m_git.branchTagExists(name, !m_isTag)) {
        m_name->setFocus();
        // CGit::GetCurrentBranch(true) falls back to the commit on a detached HEAD, which is no branch name either
        const QString exists = m_isTag ? i18n("A tag with this name already exists.") : i18n("A branch with this name already exists.");
        const QString message = !m_isTag && m_git.currentBranch() == name
            ? i18n("Cannot force updating current branch. Use reset command if you want to do so.")
            : exists + QLatin1Char(' ') + i18n("Use a different name or use the \"Force\" option to overwrite it.");
        KMessageBox::error(this, message, i18n("Warning"), noNotify);
        return false;
    }
    if (m_git.branchTagExists(name, m_isTag)) {
        // A ref of the other kind with the same name exists, so the short ref is ambiguous
        const QString warning = m_isTag ? i18n(
                                              "A branch with the same name as your entered tag name already exists (so the short ref is ambiguous).\nThis "
                                              "can cause problems so you should avoid it.")
                                        : i18n(
                                              "A tag with the same name as your entered branch name already exists (so the short ref is ambiguous).\nThis "
                                              "can cause problems so you should avoid it.");
        const auto answer = KMessageBox::warningTwoActions(this, warning, QString(), KGuiItem(i18n("Continue")), KGuiItem(i18n("A&bort")), QString(), noNotify);
        return answer == KMessageBox::PrimaryAction;
    }
    return true;
}

void CreateBranchTagDlg::onVersionChanged()
{
    // Ported from CCreateBranchTagDlg::OnVersionChanged
    if (m_chooseVersion->choice() == ChooseVersion::Choice::Branch) {
        onBranchSelectionChanged();
    } else {
        m_track->setEnabled(false);
    }
}

void CreateBranchTagDlg::onBranchSelectionChanged()
{
    // Ported from CCreateBranchTagDlg::OnCbnSelchangeComboboxexBranch: a remote branch offers its name and Track
    const QString selected = m_chooseVersion->branchCombo()->currentText();
    if (selected.startsWith(Git::remotesPrefix) && !m_isTag) {
        QString oldName = m_oldSelectBranch;
        if (const qsizetype start = oldName.lastIndexOf(QLatin1Char('/')); start >= 0) {
            oldName = oldName.mid(start + 1);
        }
        const bool isDefault = oldName == m_name->text();

        m_track->setEnabled(true);

        if (m_name->text().isEmpty() || isDefault) {
            QString name = selected;
            // Past remotes/<remote>/
            if (const qsizetype start = name.indexOf(QLatin1Char('/'), Git::remotesPrefix.size() + 1); start >= 0) {
                name = name.mid(start + 1);
            }
            m_name->setText(name);
        }
    } else {
        m_track->setEnabled(false);
    }
    m_oldSelectBranch = selected;
}

void CreateBranchTagDlg::onNameChanged()
{
    // Ported from CCreateBranchTagDlg::OnEnChangeBranchTag: a name that differs from the remote branch's stops tracking it
    if (m_chooseVersion->choice() != ChooseVersion::Choice::Branch) {
        return;
    }
    const QString name = QLatin1Char('/') + m_name->text();
    const QString remoteName = m_chooseVersion->branchCombo()->currentText();
    if (remoteName.startsWith(Git::remotesPrefix) && !remoteName.endsWith(name)) {
        m_track->setCheckState(Qt::Unchecked);
    }
}

} // namespace kgt
