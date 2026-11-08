// SPDX-FileCopyrightText: 2008-2021, 2023-2024 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitSwitchDlg.cpp and its IDD_GITSWITCH resource. TortoiseGit's
 * registry values SwitchToTagNewBranch and SwitchToCommitNewBranch go to KConfig under the same names.
 */

#include "gitswitchdlg.hpp"

#include "apputils.hpp"
#include "chooseversion.hpp"

#include <KConfigGroup>
#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <utility>

namespace
{

using namespace Qt::StringLiterals;

constexpr auto originPrefix = "origin/"_L1;
constexpr auto newBranchForTagKey = "SwitchToTagNewBranch";
constexpr auto newBranchForCommitKey = "SwitchToCommitNewBranch";
// CGit::GetShortHASHLength
constexpr qsizetype shortHashLength = 8;
constexpr KMessageBox::Options noNotify = KMessageBox::Options();

} // namespace

namespace kgt
{

GitSwitchDlg::GitSwitchDlg(Git git, const QString& initialRefName, QWidget *parent)
    : QDialog(parent)
    , m_git(std::move(git))
    , m_chooseVersion(new ChooseVersion(m_git, i18n("Switch To"), this))
    , m_branch(new QCheckBox(i18n("Create &New Branch"), this))
    , m_newBranch(new QLineEdit(this))
    , m_force(new QCheckBox(i18n("Overwrite working tree changes (&force)"), this))
    , m_merge(new QCheckBox(i18n("&Merge"), this))
    , m_track(new QCheckBox(i18n("T&rack"), this))
    , m_branchOverride(new QCheckBox(i18n("&Override branch if exists"), this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    // CAppUtils::SetWindowTitle
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Switch/Checkout", QDir::toNativeSeparators(m_git.workingDirectory())));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_chooseVersion);

    auto *optionGroup = new QGroupBox(i18n("Option"), this);
    auto *optionLayout = new QGridLayout(optionGroup);
    optionLayout->addWidget(m_branch, 0, 0);
    optionLayout->addWidget(m_newBranch, 0, 1);
    optionLayout->addWidget(m_force, 1, 0);
    optionLayout->addWidget(m_merge, 1, 1);
    optionLayout->addWidget(m_track, 2, 0, 1, 2);
    optionLayout->addWidget(m_branchOverride, 3, 0, 1, 2);
    optionLayout->setColumnStretch(1, 1);
    layout->addWidget(optionGroup);
    layout->addStretch();

    connect(m_buttons, &QDialogButtonBox::accepted, this, &GitSwitchDlg::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    // The constructor's defaults
    m_track->setTristate(true);
    m_track->setCheckState(Qt::PartiallyChecked);
    const KConfigGroup config = AppUtils::generalSettings();
    m_newBranchForTag = config.readEntry(newBranchForTagKey, true);
    m_newBranchForCommit = config.readEntry(newBranchForCommitKey, true);

    // OnInitDialog
    m_branchOverride->setToolTip(i18n("Force creation of branch/tag - even if one with same name already exists."));
    m_merge->setToolTip(i18n("Merge between original branch, working tree contents, and the branch to switch to"));
    m_track->setToolTip(
        i18n("If checked --track is passed to git, if unchecked --no-track is passed to git, else neither --track nor --no-track is passed to git (see "
             "help)."));

    // OnBnClickedCheckBranch, OnEnChangeEditBranch, and OnVersionChanged (radio buttons and combo boxes alike)
    connect(m_branch, &QCheckBox::toggled, m_newBranch, &QLineEdit::setEnabled);
    connect(m_newBranch, &QLineEdit::textChanged, this, &GitSwitchDlg::onNewBranchChanged);
    connect(m_chooseVersion, &ChooseVersion::versionChanged, this, &GitSwitchDlg::setDefaultName);

    // InitChooseVersion disables OK until the branches and tags are in
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_chooseVersion, &ChooseVersion::loaded, this, [this] {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    });
    m_chooseVersion->setInitialRefName(initialRefName);
    m_chooseVersion->init(true);

    m_chooseVersion->setDefaultChoose(ChooseVersion::Choice::Branch);
    m_branch->setFocus();
    m_track->setEnabled(false);

    resize(520, sizeHint().height());
}

const GitSwitchOptions& GitSwitchDlg::options() const
{
    return m_options;
}

ChooseVersion *GitSwitchDlg::chooseVersion() const
{
    return m_chooseVersion;
}

void GitSwitchDlg::accept()
{
    // Make sure a valid branch has been entered if a new branch is required
    const QString newBranch = m_newBranch->text().trimmed();
    if (m_branch->isChecked()) {
        // TortoiseGit shows these two as balloons on the field
        if (!Git::isBranchNameValid(newBranch)) {
            m_newBranch->setFocus();
            KMessageBox::error(this, i18n("Branch/Tag name must not be empty or is invalid."), i18n("Error"), noNotify);
            return;
        }
        if (!m_branchOverride->isChecked() && m_git.branchTagExists(newBranch)) {
            m_newBranch->setFocus();
            KMessageBox::error(this,
                               i18n("A branch with this name already exists.") + QLatin1Char(' ')
                                   + i18n("Use a different name or use the \"Override branch\" option."),
                               i18n("Warning"),
                               noNotify);
            return;
        }
        if (m_git.branchTagExists(newBranch, false)) {
            // A tag with the same name exists, so the short ref is ambiguous
            const auto answer = KMessageBox::warningTwoActions(this,
                                                               i18n("A tag with the same name as your entered branch name already exists (so the short "
                                                                    "ref is ambiguous).\nThis can cause problems so you should avoid it."),
                                                               QString(),
                                                               KGuiItem(i18n("Continue")),
                                                               KGuiItem(i18n("A&bort")),
                                                               QString(),
                                                               noNotify);
            if (answer != KMessageBox::PrimaryAction) {
                return;
            }
        }
    }

    KConfigGroup config = AppUtils::generalSettings();
    if (const ChooseVersion::Choice choice = m_chooseVersion->choice(); choice == ChooseVersion::Choice::Version) {
        m_newBranchForCommit = m_branch->isChecked();
        config.writeEntry(newBranchForCommitKey, m_newBranchForCommit);
    } else if (choice == ChooseVersion::Choice::Tag) {
        m_newBranchForTag = m_branch->isChecked();
        config.writeEntry(newBranchForTagKey, m_newBranchForTag);
    }
    config.sync();

    m_options = GitSwitchOptions{
        .versionName = m_chooseVersion->versionName(),
        .isNewBranch = m_branch->isChecked(),
        .newBranch = newBranch,
        .force = m_force->isChecked(),
        .merge = m_merge->isChecked(),
        .track = m_track->checkState(),
        .branchOverride = m_branchOverride->isChecked(),
    };
    QDialog::accept();
}

void GitSwitchDlg::setDefaultName()
{
    // Ported from CGitSwitchDlg::SetDefaultName
    const QString versionName = m_chooseVersion->versionName();
    const ChooseVersion::Choice choice = m_chooseVersion->choice();
    QString version = versionName;

    qsizetype start = -1;
    if (version.startsWith(originPrefix)) {
        start = version.indexOf(QLatin1Char('/'), originPrefix.size() + 1);
    } else if (version.startsWith(Git::remotesPrefix)) {
        start = version.indexOf(QLatin1Char('/'), Git::remotesPrefix.size() + 1);
    }

    bool branch = false;
    Qt::CheckState track = Qt::Unchecked;
    if (start >= 0) {
        // A remote branch: a local branch of the same name, tracking it
        version = version.mid(start + 1);
        m_track->setEnabled(true);
        branch = true;
        track = Qt::PartiallyChecked;
    } else {
        if (versionName.startsWith(Git::refsHeadsPrefix)) {
            version = versionName.mid(Git::refsHeadsPrefix.size());
        } else if (choice == ChooseVersion::Choice::Version) {
            version = version.left(shortHashLength);
        }
        version = QStringLiteral("Branch_") + version;
        m_track->setEnabled(false);
    }
    {
        // UpdateData(FALSE) sets the name before OnEnChangeEditBranch could see the new choice
        const QSignalBlocker blocker(m_newBranch);
        m_newBranch->setText(version);
    }

    if ((choice == ChooseVersion::Choice::Version && m_newBranchForCommit) || (choice == ChooseVersion::Choice::Tag && m_newBranchForTag)) {
        branch = true;
    }

    m_newBranch->setEnabled(branch);
    m_branchOverride->setEnabled(branch);
    if (!branch) {
        m_branchOverride->setChecked(false);
        m_track->setEnabled(false);
        track = Qt::Unchecked;
    }
    m_branch->setChecked(branch);
    m_track->setCheckState(track);
}

void GitSwitchDlg::onNewBranchChanged()
{
    // Ported from CGitSwitchDlg::OnEnChangeEditBranch: a name that differs from the remote branch's stops tracking it
    if (m_chooseVersion->choice() != ChooseVersion::Choice::Branch) {
        return;
    }
    const QString name = QLatin1Char('/') + m_newBranch->text();
    const QString remoteName = m_chooseVersion->branchCombo()->currentText();
    if (remoteName.startsWith(Git::remotesPrefix) && !remoteName.endsWith(name)) {
        m_track->setCheckState(Qt::Unchecked);
    }
}

} // namespace kgt
