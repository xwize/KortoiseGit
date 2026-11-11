// SPDX-FileCopyrightText: 2008-2024, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/PushDlg.cpp and its IDD_PUSH resource, with CGit::GetRemotePushBranch.
 */

#include "pushdlg.hpp"

#include "apputils.hpp"
#include "historycombo.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <tuple>

namespace
{

using namespace kgt;

// Like TortoiseGit's message boxes, these don't also raise a desktop notification
constexpr KMessageBox::Options noNotify = KMessageBox::Options();

/**
 * @param output Git's output, one name per line.
 * @returns The non-empty lines.
 */
QStringList lines(const QByteArray& output)
{
    return QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

/**
 * @param combo A combo box.
 * @param text An entry's text.
 */
void selectText(QComboBox *combo, const QString& text)
{
    const int index = combo->findText(text);
    if (index >= 0) {
        combo->setCurrentIndex(index);
    } else {
        combo->setEditText(text);
    }
}

/**
 * @param parent The dialog.
 * @param question A question.
 * @param warning Whether it warns of something, such as a deletion, rather than only asking.
 * @returns Whether the user answered Yes.
 */
bool askYesNo(QWidget *parent, const QString& question, bool warning)
{
    const KGuiItem yesItem(i18n("&Yes"));
    const KGuiItem noItem(i18n("&No"));
    const auto answer = warning ? KMessageBox::warningTwoActions(parent, question, QString(), yesItem, noItem, QString(), noNotify)
                                : KMessageBox::questionTwoActions(parent, question, QString(), yesItem, noItem, QString(), noNotify);
    return answer == KMessageBox::PrimaryAction;
}

} // namespace

namespace kgt
{

PushDlg::PushDlg(const QString& workingTree, QWidget *parent, const QString& selectLocalBranch)
    : QDialog(parent)
    , m_git(workingTree)
    , m_selectLocalBranch(selectLocalBranch)
    , m_branchSource(new QComboBox(this))
    , m_branchRemote(new QComboBox(this))
    , m_remoteRadio(new QRadioButton(i18n("Re&mote:"), this))
    , m_urlRadio(new QRadioButton(i18n("Arbitrary &URL:"), this))
    , m_remote(new QComboBox(this))
    , m_remoteManage(new QPushButton(i18n("Mana&ge"), this))
    , m_remoteUrl(new QComboBox(this))
    , m_forceWithLease(new QCheckBox(i18n("Force &with lease"), this))
    , m_tags(new QCheckBox(i18n("Include &Tags"), this))
    , m_setUpstream(new QCheckBox(i18n("&Set upstream/track remote branch"), this))
{
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Push", QDir::toNativeSeparators(workingTree)));

    auto *layout = new QVBoxLayout(this);

    auto *refGroup = new QGroupBox(i18n("Ref"), this);
    auto *refLayout = new QFormLayout(refGroup);
    m_branchSource->setEditable(true);
    m_branchSource->setInsertPolicy(QComboBox::NoInsert);
    m_branchRemote->setEditable(true);
    m_branchRemote->setInsertPolicy(QComboBox::NoInsert);
    refLayout->addRow(i18n("&Local:"), m_branchSource);
    refLayout->addRow(i18n("&Remote:"), m_branchRemote);
    layout->addWidget(refGroup);

    auto *destinationGroup = new QGroupBox(i18n("Destination"), this);
    auto *destinationLayout = new QGridLayout(destinationGroup);
    destinationLayout->addWidget(m_remoteRadio, 0, 0);
    destinationLayout->addWidget(m_remote, 0, 1);
    destinationLayout->addWidget(m_remoteManage, 0, 2);
    destinationLayout->addWidget(m_urlRadio, 1, 0);
    destinationLayout->addWidget(m_remoteUrl, 1, 1, 1, 2);
    destinationLayout->setColumnStretch(1, 1);
    layout->addWidget(destinationGroup);

    // OnInitDialog: the URL history, disabled until "Arbitrary URL" is chosen
    m_urlRadio->setObjectName(QStringLiteral("urlRadio"));
    m_remoteUrl->setObjectName(QStringLiteral("remoteUrl"));
    m_remoteUrl->setEditable(true);
    m_remoteUrl->setInsertPolicy(QComboBox::NoInsert);
    HistoryCombo::loadHistory(m_remoteUrl, urlHistory(), QStringLiteral("url"));
    m_remoteUrl->setEnabled(false);
    m_remoteRadio->setChecked(true);

    auto *optionGroup = new QGroupBox(i18n("Options"), this);
    auto *optionLayout = new QVBoxLayout(optionGroup);
    m_forceWithLease->setToolTip(
        i18n("Force push only if the remote reference points to the same commit as the remote tracking branch.\nThis can cause the remote repository "
             "to lose commits; use it with care.\nThis option corresponds to the --force-with-lease git option."));
    optionLayout->addWidget(m_forceWithLease);
    optionLayout->addWidget(m_tags);
    optionLayout->addWidget(m_setUpstream);
    layout->addWidget(optionGroup);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PushDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_branchSource, &QComboBox::currentTextChanged, this, &PushDlg::selectRemoteBranch);
    connect(m_forceWithLease, &QCheckBox::toggled, this, &PushDlg::updateOptionStates);
    connect(m_tags, &QCheckBox::toggled, this, &PushDlg::updateOptionStates);
    connect(m_remoteRadio, &QRadioButton::toggled, this, &PushDlg::onSourceChanged);
    connect(m_remoteManage, &QPushButton::clicked, this, &PushDlg::onRemoteManage);

    // PreTranslateMessage's VK_F5. A failure is shown, as Refresh does, since nothing above the event loop catches it
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, [this] {
        try {
            refresh(m_selectLocalBranch);
        } catch (const RuntimeError& e) {
            KMessageBox::error(this, e.error().message, QString(), noNotify);
        }
    });

    refresh(selectLocalBranch);
    resize(520, sizeHint().height());
}

PushOptions PushDlg::options() const
{
    return {
        .localBranch = m_branchSource->currentText().trimmed(),
        .remote = m_remoteRadio->isChecked() ? m_remote->currentText() : m_remoteUrl->currentText().trimmed(),
        .remoteBranch = m_branchRemote->currentText().trimmed(),
        .forceWithLease = m_forceWithLease->isEnabled() && m_forceWithLease->isChecked(),
        .tags = m_tags->isEnabled() && m_tags->isChecked(),
        .setUpstream = m_setUpstream->isEnabled() && m_setUpstream->isChecked(),
    };
}

void PushDlg::accept()
{
    const PushOptions chosen = options();
    // OnBnClickedOk checks only the named remote; git itself reports an empty URL
    if (m_remoteRadio->isChecked() && chosen.remote.isEmpty()) {
        KMessageBox::error(this, i18n("Remote name must not be empty."), QString(), noNotify);
        return;
    }
    if (chosen.localBranch.isEmpty() && chosen.remoteBranch.isEmpty()) {
        if (!askYesNo(this, i18n("The local branch name and the remote branch name are empty.\nContinue?"), false)) {
            return;
        }
    } else if (chosen.localBranch.isEmpty()) {
        if (!askYesNo(this, i18n("The local branch/tag name is empty. This results in a remote removal.\nContinue?"), true)) {
            return;
        }
    } else if (!chosen.remoteBranch.isEmpty() && !isBranchNameValid(chosen.remoteBranch)) {
        KMessageBox::error(this, i18n("Branch/Tag name is invalid."), QString(), noNotify);
        return;
    } else if (m_remoteUrl->isEnabled()) {
        // Remote URLs are stored only if used
        HistoryCombo::saveHistory(m_remoteUrl, urlHistory(), QStringLiteral("url"));
    }
    QDialog::accept();
}

void PushDlg::refresh(const QString& selectLocalBranch)
{
    const QStringList remotes = lines(m_git.run({QStringLiteral("remote")}, RunMode::ReadOnly));
    m_remote->clear();
    m_remote->addItems(remotes);
    m_remote->setCurrentIndex(-1);

    const QStringList branches =
        lines(m_git.run({QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"), QStringLiteral("refs/heads/")}, RunMode::ReadOnly));
    QString current;
    try {
        current = QString::fromUtf8(
                      m_git.run({QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("-q"), QStringLiteral("HEAD")}, RunMode::ReadOnly))
                      .trimmed();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // A detached HEAD: TortoiseGit then pushes HEAD itself
        current = QStringLiteral("HEAD");
    }
    {
        // Don't look up the remote branch for each item added
        const QSignalBlocker blocker(m_branchSource);
        m_branchSource->clear();
        m_branchSource->addItems(branches);
        // CPushDlg::Refresh's m_BranchSourceName
        static const QString headsPrefix = QStringLiteral("refs/heads/");
        if (selectLocalBranch.isEmpty()) {
            selectText(m_branchSource, current);
        } else if (selectLocalBranch.startsWith(headsPrefix)) {
            selectText(m_branchSource, selectLocalBranch.mid(headsPrefix.size()));
        } else {
            // A ref such as refs/tags/v1, or a commit, is not in the list of branches
            m_branchSource->setEditText(selectLocalBranch);
        }
    }
    selectRemoteBranch(m_branchSource->currentText());

    // If the current branch isn't tracked yet, tracking it is the default
    if (m_git.configValue(QStringLiteral("branch.%1.remote").arg(current)).isEmpty()
        && m_git.configValue(QStringLiteral("branch.%1.merge").arg(current)).isEmpty()) {
        m_setUpstream->setChecked(true);
    }
    updateOptionStates();
    enDisablePushRemoteArchiveBranch();
}

void PushDlg::selectRemoteBranch(const QString& localBranch)
{
    const QString branch = localBranch.trimmed();
    if (branch.isEmpty()) {
        enDisablePushRemoteArchiveBranch();
        return;
    }
    // CGit::GetRemotePushBranch
    QString pushRemote = m_git.configValue(QStringLiteral("branch.%1.pushremote").arg(branch));
    if (pushRemote.isEmpty()) {
        pushRemote = m_git.configValue(QStringLiteral("remote.pushdefault"));
    }
    if (pushRemote.isEmpty()) {
        pushRemote = m_git.configValue(QStringLiteral("branch.%1.remote").arg(branch));
    }
    // Not stripped, for gerrit's refs/for/..., as TortoiseGit's issue #1609
    QString pushBranch = m_git.configValue(QStringLiteral("branch.%1.pushbranch").arg(branch));
    if (pushBranch.isEmpty()) {
        pushBranch = Git::stripRefName(m_git.configValue(QStringLiteral("branch.%1.merge").arg(branch)));
    }

    if (const int index = m_remote->findText(pushRemote); !pushRemote.isEmpty() && index >= 0) {
        m_remote->setCurrentIndex(index);
    } else if (m_remote->count() == 1) {
        m_remote->setCurrentIndex(0);
    } else {
        m_remote->setCurrentIndex(-1);
    }

    // TortoiseGit falls back to the branch history; without one, the same name as the local branch
    m_branchRemote->setEditText(pushBranch.isEmpty() && branch != QLatin1String("HEAD") ? branch : pushBranch);
    enDisablePushRemoteArchiveBranch();
}

void PushDlg::updateOptionStates()
{
    // --force-with-lease and --tags don't go together
    m_tags->setEnabled(!m_forceWithLease->isChecked());
    m_forceWithLease->setEnabled(!m_tags->isChecked());
}

void PushDlg::enDisablePushRemoteArchiveBranch()
{
    // Without "Push all branches" and the push-remote/push-branch options, only Set upstream is left to decide
    const bool enableUpstream = m_remoteRadio->isChecked() && !m_branchSource->currentText().trimmed().isEmpty();
    if (!enableUpstream) {
        m_setUpstream->setChecked(false);
    }
    m_setUpstream->setEnabled(enableUpstream);
}

void PushDlg::onSourceChanged()
{
    if (m_remoteRadio->isChecked()) {
        m_remote->setEnabled(true);
        m_remoteManage->setEnabled(true);
        m_remoteUrl->setEnabled(false);
        enDisablePushRemoteArchiveBranch();
        return;
    }

    // Similar code in PullFetchDlg::onSourceChanged
    QString clipPath = AppUtils::getClipboardLink(QStringLiteral("git pull"), 1);
    if (clipPath.isEmpty()) {
        clipPath = AppUtils::getClipboardLink(QStringLiteral("git fetch"), 1);
    }
    const auto unquote = [](QString text) {
        if (text.size() > 2
            && ((text.front() == QLatin1Char('"') && text.back() == QLatin1Char('"'))
                || (text.front() == QLatin1Char('\'') && text.back() == QLatin1Char('\'')))) {
            text = text.mid(1, text.size() - 2);
        }
        return text;
    };
    if (clipPath.isEmpty()) {
        if (m_remoteUrl->count() > 0) {
            m_remoteUrl->setCurrentIndex(0);
        }
    } else if (const qsizetype separator = clipPath.indexOf(QLatin1Char(' ')); separator > 1 && clipPath.size() > separator + 2) {
        m_remoteUrl->setEditText(unquote(clipPath.left(separator)));
        m_branchRemote->setEditText(unquote(clipPath.mid(separator + 1)));
    } else {
        m_remoteUrl->setEditText(clipPath);
    }
    m_remote->setEnabled(false);
    m_remoteManage->setEnabled(false);
    m_remoteUrl->setEnabled(true);
    enDisablePushRemoteArchiveBranch();
}

void PushDlg::onRemoteManage()
{
    AppUtils::launchRemoteSetting(m_git.workingDirectory(), this);
    try {
        refresh(m_selectLocalBranch);
    } catch (const RuntimeError& e) {
        KMessageBox::error(this, e.error().message, QString(), noNotify);
    }
}

KConfigGroup PushDlg::urlHistory() const
{
    return HistoryCombo::historyGroup(QStringLiteral("PushURLS")).group(m_git.workingDirectory());
}

bool PushDlg::isBranchNameValid(const QString& name) const
{
    try {
        std::ignore = m_git.run({QStringLiteral("check-ref-format"), QStringLiteral("--branch"), name}, RunMode::ReadOnly);
        return true;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return false;
    }
}

} // namespace kgt
