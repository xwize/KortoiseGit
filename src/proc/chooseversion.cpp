// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ChooseVersion.h (CChooseVersion) and the IDC_RADIO_BRANCH ...
 * IDC_BUTTON_SHOW controls its hosts' resources share.
 */

#include "chooseversion.hpp"

#include "browserefsdlg.hpp"
#include "git/gitcall.hpp"
#include "kortoisegitproc_debug.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QComboBox>
#include <QGridLayout>
#include <QPushButton>
#include <QRadioButton>

#include <utility>

namespace
{

using namespace Qt::StringLiterals;
using namespace kgt;

constexpr auto refsPrefix = "refs/"_L1;
constexpr auto headsPrefix = "heads/"_L1;
constexpr auto remotesPrefix = "remotes/"_L1;
constexpr auto tagsPrefix = "tags/"_L1;
constexpr auto peeledSuffix = "^{}"_L1;

/**
 * @param call A finished git call.
 * @returns Its output, or nothing if git failed: TortoiseGit's LoadingThread ignores the lists' errors too.
 */
QByteArray outputOrEmpty(const GitCall& call)
{
    try {
        return call.output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        qCWarning(KGT_PROC) << "Loading the branches or tags failed:" << e.error().message;
        return {};
    }
}

/**
 * Ported from CHistoryCombo::SetList.
 * @param combo A combo box.
 * @param entries What it lists from now on.
 */
void setList(QComboBox *combo, const QStringList& entries)
{
    combo->clear();
    combo->addItems(entries);
}

} // namespace

namespace kgt
{

ChooseVersion::ChooseVersion(Git git, const QString& title, QWidget *parent)
    : QGroupBox(title, parent)
    , m_git(std::move(git))
    , m_radioBranch(new QRadioButton(i18n("&Branch"), this))
    , m_radioTag(new QRadioButton(i18n("&Tag"), this))
    , m_radioVersion(new QRadioButton(i18n("&Commit"), this))
    , m_branch(new QComboBox(this))
    , m_tags(new QComboBox(this))
    , m_version(new QComboBox(this))
    , m_browseRef(new QPushButton(QStringLiteral("..."), this))
    , m_showLog(new QPushButton(QStringLiteral("..."), this))
{
    m_version->setEditable(true);
    m_version->setInsertPolicy(QComboBox::NoInsert);
    for (QComboBox *combo : {m_branch, m_tags, m_version}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(30);
    }
    for (QPushButton *button : {m_browseRef, m_showLog}) {
        button->setFixedWidth(button->fontMetrics().horizontalAdvance(QStringLiteral("...")) * 3);
    }
    // OnBnClickedChooseVersion opens the log as a picker, which LogDlg doesn't offer yet: a hash is typed instead
    m_showLog->setToolTip(i18n("Type a commit hash; picking it from the log is not available yet."));

    auto *layout = new QGridLayout(this);
    layout->addWidget(m_radioBranch, 0, 0);
    layout->addWidget(m_branch, 0, 1);
    layout->addWidget(m_browseRef, 0, 2);
    layout->addWidget(m_radioTag, 1, 0);
    layout->addWidget(m_tags, 1, 1);
    layout->addWidget(m_radioVersion, 2, 0);
    layout->addWidget(m_version, 2, 1);
    layout->addWidget(m_showLog, 2, 2);
    layout->setColumnStretch(1, 1);

    for (const QRadioButton *button : {m_radioBranch, m_radioTag, m_radioVersion}) {
        connect(button, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                onChooseRadio();
            }
        });
    }
    connect(m_browseRef, &QPushButton::clicked, this, &ChooseVersion::onBrowseRef);
    for (const QComboBox *combo : {m_branch, m_tags, m_version}) {
        connect(combo, &QComboBox::currentTextChanged, this, &ChooseVersion::versionChanged);
    }

    m_radioBranch->setChecked(true);
    onChooseRadio();
}

ChooseVersion::~ChooseVersion() = default;

void ChooseVersion::setInitialRefName(const QString& refName)
{
    m_initialRefName = refName;
}

void ChooseVersion::setSkipCurrentBranch(bool skip)
{
    m_skipCurrentBranch = skip;
}

void ChooseVersion::init(bool setFocusToBranchComboBox)
{
    m_pendingRefName = m_initialRefName;
    m_notFullName = true;
    m_isFirstTimeToSetFocus = setFocusToBranchComboBox;

    // InitChooseVersion: no branch or tag can be chosen until they are loaded
    m_radioBranch->setEnabled(false);
    m_radioTag->setEnabled(false);

    // LoadingThread, with two git calls in place of the thread; BranchesIncludeFetchHead is on by default
    m_branchCall = m_git.runAsync(Git::branchListArguments(BranchAllF), RunMode::ReadOnly);
    m_tagCall = m_git.runAsync({QStringLiteral("tag"), QStringLiteral("-l")}, RunMode::ReadOnly);
    connect(m_branchCall.get(), &GitCall::finished, this, &ChooseVersion::onLoadFinished);
    connect(m_tagCall.get(), &GitCall::finished, this, &ChooseVersion::onLoadFinished);
}

bool ChooseVersion::isLoading() const
{
    return m_branchCall != nullptr;
}

void ChooseVersion::setDefaultChoose(Choice choice)
{
    QRadioButton *button = radio(choice);
    if (button->isChecked()) {
        onChooseRadio();
    } else {
        button->setChecked(true);
    }
}

ChooseVersion::Choice ChooseVersion::choice() const
{
    if (m_radioTag->isChecked()) {
        return Choice::Tag;
    }
    if (m_radioVersion->isChecked()) {
        return Choice::Version;
    }
    return Choice::Branch;
}

QString ChooseVersion::versionName() const
{
    switch (choice()) {
    case Choice::Branch: {
        const QString name = m_branch->currentIndex() < 0 ? QString() : m_branch->currentText();
        if (!name.isEmpty() && !m_git.isBranchTagNameUnique(name)) {
            return QStringLiteral("refs/heads/") + name;
        }
        return name;
    }
    case Choice::Tag: {
        const QString name = m_tags->currentIndex() < 0 ? QString() : m_tags->currentText();
        if (!name.isEmpty() && !m_git.isBranchTagNameUnique(name)) {
            return QStringLiteral("refs/tags/") + name;
        }
        return name;
    }
    case Choice::Version:
        return m_version->currentText().trimmed();
    }
    return {};
}

bool ChooseVersion::isBranch() const
{
    return m_radioBranch->isChecked();
}

bool ChooseVersion::isBranchEmpty() const
{
    return m_radioBranch->isChecked() && m_branch->currentIndex() < 0;
}

void ChooseVersion::selectRef(QString refName, bool refNameIsPossiblyNotFullName)
{
    if (refNameIsPossiblyNotFullName) {
        // Make sure refName is a full ref name first
        if (const QString fullRefName = m_git.fullRefName(refName); !fullRefName.isEmpty()) {
            refName = fullRefName;
        }
    }

    if (refName.startsWith(refsPrefix)) {
        refName = refName.mid(refsPrefix.size());
    }
    if (refName.startsWith(headsPrefix)) {
        refName = refName.mid(headsPrefix.size());
        setDefaultChoose(Choice::Branch);
        m_branch->setCurrentIndex(m_branch->findText(refName, Qt::MatchExactly | Qt::MatchCaseSensitive));
    } else if (refName.startsWith(remotesPrefix)) {
        setDefaultChoose(Choice::Branch);
        m_branch->setCurrentIndex(m_branch->findText(refName, Qt::MatchExactly | Qt::MatchCaseSensitive));
    } else if (refName.startsWith(tagsPrefix)) {
        refName = refName.mid(tagsPrefix.size());
        if (refName.endsWith(peeledSuffix)) {
            refName.chop(peeledSuffix.size());
        }
        setDefaultChoose(Choice::Tag);
        m_tags->setCurrentIndex(m_tags->findText(refName, Qt::MatchExactly | Qt::MatchCaseSensitive));
    } else {
        setDefaultChoose(Choice::Version);
        // CHistoryCombo::AddString: the commit goes in once and is selected
        int index = m_version->findText(refName, Qt::MatchExactly | Qt::MatchCaseSensitive);
        if (index < 0) {
            m_version->addItem(refName);
            index = m_version->count() - 1;
        }
        m_version->setCurrentIndex(index);
    }
    Q_EMIT versionChanged();
}

const QString& ChooseVersion::currentBranch() const
{
    return m_currentBranch;
}

QComboBox *ChooseVersion::branchCombo() const
{
    return m_branch;
}

QComboBox *ChooseVersion::tagCombo() const
{
    return m_tags;
}

QComboBox *ChooseVersion::versionCombo() const
{
    return m_version;
}

QPushButton *ChooseVersion::browseRefButton() const
{
    return m_browseRef;
}

void ChooseVersion::onChooseRadio()
{
    const Choice chosen = choice();
    m_branch->setEnabled(chosen == Choice::Branch);
    m_browseRef->setEnabled(chosen == Choice::Branch);
    m_tags->setEnabled(chosen == Choice::Tag);
    m_version->setEnabled(chosen == Choice::Version);
    // TortoiseGit enables IDC_BUTTON_SHOW with Commit; it stays off until LogDlg can pick a commit
    m_showLog->setEnabled(false);
    Q_EMIT versionChanged();
}

void ChooseVersion::onBrowseRef()
{
    const QString versionName = this->versionName();
    const QString resultRef = BrowseRefsDlg::pickRef(m_git.workingDirectory(), {.initialRef = versionName, .pickRefKind = PickRefAll}, this);
    // InitChooseVersion(true, true): reload, as refs may have been created or deleted, then select the result
    init(true);
    if (resultRef.isEmpty()) {
        m_pendingRefName = versionName;
        m_notFullName = true;
        return;
    }
    m_pendingRefName = resultRef;
    m_notFullName = false;
}

void ChooseVersion::onLoadFinished()
{
    if (!m_branchCall || !m_tagCall || !m_branchCall->isFinished() || !m_tagCall->isFinished()) {
        return;
    }
    // Deleted from the event loop: one of them is still emitting
    GitCall *branchCall = m_branchCall.release();
    GitCall *tagCall = m_tagCall.release();
    branchCall->deleteLater();
    tagCall->deleteLater();

    const BranchList branches = Git::parseBranchList(outputOrEmpty(*branchCall), BranchAllF, m_skipCurrentBranch, !m_git.derefFetchHead().isEmpty());
    updateGui(branches, Git::parseTagList(outputOrEmpty(*tagCall)));
}

void ChooseVersion::updateGui(const BranchList& branches, const QStringList& tags)
{
    {
        const QSignalBlocker branchBlocker(m_branch);
        const QSignalBlocker tagBlocker(m_tags);
        setList(m_branch, branches.branches);
        m_branch->setCurrentIndex(branches.current);
        setList(m_tags, tags);
        m_tags->setCurrentIndex(tags.isEmpty() ? -1 : 0);
    }
    m_currentBranch = m_git.currentBranch();

    m_radioBranch->setEnabled(true);
    m_radioTag->setEnabled(true);

    if (m_pendingRefName.isEmpty()) {
        Q_EMIT versionChanged();
    } else {
        selectRef(m_pendingRefName, m_notFullName);
    }

    if (m_isFirstTimeToSetFocus) {
        for (QComboBox *combo : {m_branch, m_tags, m_version}) {
            if (combo->isEnabled()) {
                combo->setFocus();
                break;
            }
        }
    }
    m_isFirstTimeToSetFocus = false;
    Q_EMIT loaded();
}

QRadioButton *ChooseVersion::radio(Choice choice) const
{
    switch (choice) {
    case Choice::Branch:
        return m_radioBranch;
    case Choice::Tag:
        return m_radioTag;
    case Choice::Version:
        return m_radioVersion;
    }
    return m_radioBranch;
}

} // namespace kgt
