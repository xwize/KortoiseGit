// SPDX-FileCopyrightText: 2008-2022, 2024-2025 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/PullFetchDlg.cpp and its IDD_PULLFETCH resource, with
 * CGit::GetRemoteTrackedBranch. TortoiseGit's registry values go to KConfig under the same names.
 */

#include "pullfetchdlg.hpp"

#include "apputils.hpp"
#include "git/gitadmindir.hpp"
#include "utils/runtimeerror.hpp"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace
{

using namespace kgt;

// CHistoryCombo's MAX_HISTORY_ITEMS
constexpr qsizetype maxHistoryItems = 25;

/**
 * A history of a CHistoryCombo, such as TortoiseGit's History\PullURLS with its entries url0, url1, ...
 */
struct History {
    QLatin1StringView name; // The history's name, such as "PullURLS"
    QLatin1StringView key; // The key prefix TortoiseGit uses, such as "url"; here one list
};

constexpr History pullUrls{.name = QLatin1StringView("PullURLS"), .key = QLatin1StringView("url")};
constexpr History pullRemoteBranches{.name = QLatin1StringView("PullRemoteBranch"), .key = QLatin1StringView("br")};

/**
 * @param name A history, such as TortoiseGit's History\PullURLS.
 * @returns Where it is kept.
 */
KConfigGroup historyGroup(const QString& name)
{
    return KSharedConfig::openConfig()->group(QStringLiteral("History")).group(name);
}

/**
 * Ported from CHistoryCombo::LoadHistory.
 * @param combo An editable combo box.
 * @param history The history.
 */
void loadHistory(QComboBox *combo, const History& history)
{
    combo->addItems(historyGroup(history.name).readEntry(QString(history.key), QStringList()));
}

/**
 * Ported from CHistoryCombo::SaveHistory: the text shown goes first, without duplicates.
 * @param combo An editable combo box.
 * @param history The history.
 */
void saveHistory(const QComboBox *combo, const History& history)
{
    const QString current = combo->currentText().trimmed();
    if (current.isEmpty()) {
        return;
    }
    KConfigGroup group = historyGroup(history.name);
    QStringList entries = group.readEntry(QString(history.key), QStringList());
    entries.removeAll(current);
    entries.prepend(current);
    group.writeEntry(QString(history.key), entries.mid(0, maxHistoryItems));
    group.sync();
}

/**
 * Ported from CHistoryCombo::AddString: appends an entry unless it is there already, and selects it.
 * @param combo A combo box.
 * @param text The entry; nothing happens if it is empty.
 */
void addString(QComboBox *combo, const QString& text)
{
    if (text.isEmpty()) {
        return;
    }
    int index = combo->findText(text, Qt::MatchExactly | Qt::MatchCaseSensitive);
    if (index < 0) {
        combo->addItem(text);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

/**
 * @param text A checkbox's label.
 * @param tristate Whether it has a third, "git's default", state.
 * @param parent Its parent.
 * @returns The checkbox.
 */
QCheckBox *checkBox(const QString& text, bool tristate, QWidget *parent)
{
    auto *box = new QCheckBox(text, parent);
    box->setTristate(tristate);
    if (tristate) {
        box->setCheckState(Qt::PartiallyChecked);
    }
    return box;
}

/**
 * @returns IDS_PROC_PUSHFETCH_ALLREMOTES, the remote entry that fetches from all of them.
 */
QString allRemotesText()
{
    return i18n("- all -");
}

} // namespace

namespace kgt
{

PullFetchDlg::PullFetchDlg(const QString& workingDirectory, bool isPull, QWidget *parent)
    : QDialog(parent)
    , m_git(workingDirectory)
    , m_isPull(isPull)
    , m_remoteRadio(new QRadioButton(i18n("&Remote:"), this))
    , m_otherRadio(new QRadioButton(i18n("Arbitrary &URL:"), this))
    , m_remote(new QComboBox(this))
    , m_other(new QComboBox(this))
    , m_remoteBranch(new QComboBox(this))
    , m_squash(checkBox(i18n("&Squash"), false, this))
    , m_noCommit(checkBox(i18n("No Co&mmit"), false, this))
    , m_depthCheck(checkBox(i18n("Depth"), false, this))
    , m_depth(new QSpinBox(this))
    , m_noFF(checkBox(i18n("No &Fast Forward"), false, this))
    , m_ffOnly(checkBox(i18n("Fast Forward O&nly"), false, this))
    , m_fetchTags(checkBox(i18n("Tags"), true, this))
    , m_tagOpt(new QLabel(this))
    , m_prune(checkBox(i18n("Prune"), true, this))
    , m_pruneDefault(new QLabel(this))
    , m_rebase(checkBox(i18n("&Launch Rebase After Fetch"), false, this))
{
    // IDS_PROGRS_TITLE_PULL / IDS_PROGRS_TITLE_FETCH, through CAppUtils::SetWindowTitle
    setWindowTitle(isPull ? i18nc("@title:window %1 is a folder", "%1 - Pull", QDir::toNativeSeparators(workingDirectory))
                          : i18nc("@title:window %1 is a folder", "%1 - Fetch", QDir::toNativeSeparators(workingDirectory)));

    auto *layout = new QVBoxLayout(this);

    auto *remoteGroup = new QGroupBox(i18n("Remote"), this);
    auto *remoteLayout = new QGridLayout(remoteGroup);
    m_other->setEditable(true);
    m_other->setInsertPolicy(QComboBox::NoInsert);
    m_remoteBranch->setEditable(true);
    m_remoteBranch->setInsertPolicy(QComboBox::NoInsert);
    auto *branchLabel = new QLabel(i18n("Remote &Branch:"), remoteGroup);
    branchLabel->setBuddy(m_remoteBranch);
    remoteLayout->addWidget(m_remoteRadio, 0, 0);
    remoteLayout->addWidget(m_remote, 0, 1);
    remoteLayout->addWidget(m_otherRadio, 1, 0);
    remoteLayout->addWidget(m_other, 1, 1);
    remoteLayout->addWidget(branchLabel, 2, 0);
    remoteLayout->addWidget(m_remoteBranch, 2, 1);
    remoteLayout->setColumnStretch(1, 1);
    layout->addWidget(remoteGroup);

    // In TortoiseGit's grid: Squash, No Commit, Depth / No Fast Forward, Fast Forward Only / Tags / Prune
    auto *optionGroup = new QGroupBox(i18n("Options"), this);
    auto *optionLayout = new QGridLayout(optionGroup);
    m_depth->setRange(1, std::numeric_limits<int>::max());
    m_depth->setValue(1); // m_nDepth
    auto *depthLayout = new QHBoxLayout;
    depthLayout->addWidget(m_depthCheck);
    depthLayout->addWidget(m_depth);
    depthLayout->addStretch();
    optionLayout->addWidget(m_squash, 0, 0);
    optionLayout->addWidget(m_noCommit, 0, 1);
    optionLayout->addLayout(depthLayout, 0, 2);
    optionLayout->addWidget(m_noFF, 1, 0);
    optionLayout->addWidget(m_ffOnly, 1, 1);
    optionLayout->addWidget(m_fetchTags, 2, 0);
    optionLayout->addWidget(m_tagOpt, 2, 1);
    optionLayout->addWidget(m_prune, 3, 0);
    optionLayout->addWidget(m_pruneDefault, 3, 1);
    optionLayout->setColumnStretch(2, 1);
    layout->addWidget(optionGroup);

    layout->addWidget(m_rebase);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PullFetchDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // OnInitDialog
    const KConfigGroup settings = KSharedConfig::openConfig()->group(settingsGroup());

    // TortoiseGit fetches and then opens RebaseDlg rather than running "git pull --rebase" (CAppUtils::Pull,
    // RebaseAfterFetch), also when branch.<name>.rebase or pull.rebase asks for it. Until RebaseDlg exists
    // (KORTOISEGIT-0018) the option stays off and disabled, and its "rebase" setting is neither read nor written.
    m_rebase->setChecked(false);
    m_rebase->setEnabled(false);

    m_remoteRadio->setChecked(true);
    m_other->setEnabled(false);
    // m_bNamedRemoteFetchAll, on by default: fetching from a named remote uses its refspecs
    if (!m_isPull) {
        m_remoteBranch->setEnabled(false);
    }

    if (m_isPull) {
        m_ffOnly->setChecked(settings.readEntry("ffonly", false));
        updateFastForwardStates();
    } else {
        m_squash->setEnabled(false);
        m_noFF->setEnabled(false);
        m_ffOnly->setEnabled(false);
        m_noCommit->setEnabled(false);
    }

    // The depth is offered for a shallow repository only, ticked
    const bool shallow = QString::fromUtf8(m_git.run({QStringLiteral("rev-parse"), QStringLiteral("--is-shallow-repository")}, RunMode::ReadOnly)).trimmed()
        == QLatin1String("true");
    m_depthCheck->setVisible(shallow);
    m_depth->setVisible(shallow);
    m_depthCheck->setChecked(shallow);
    m_depth->setEnabled(shallow);

    loadHistory(m_other, pullUrls);
    loadHistory(m_remoteBranch, pullRemoteBranches);
    if (m_remoteBranch->count() > 0) {
        m_remoteBranch->setCurrentIndex(0);
    }

    connect(m_remote, &QComboBox::currentIndexChanged, this, &PullFetchDlg::onRemoteChanged);
    connect(m_remoteRadio, &QRadioButton::toggled, this, &PullFetchDlg::onSourceChanged);
    connect(m_depthCheck, &QCheckBox::toggled, m_depth, &QWidget::setEnabled);
    connect(m_noFF, &QCheckBox::toggled, this, &PullFetchDlg::updateFastForwardStates);
    connect(m_ffOnly, &QCheckBox::toggled, this, &PullFetchDlg::updateFastForwardStates);

    refresh();
    resize(520, sizeHint().height());
}

PullFetchOptions PullFetchDlg::options() const
{
    PullFetchOptions chosen{
        .remoteUrl = {},
        .remoteBranchName = {},
        .allRemotes = false,
        .rebase = m_rebase->isEnabled() && m_rebase->isChecked(),
        .noFF = m_noFF->isEnabled() && m_noFF->isChecked(),
        .ffOnly = m_ffOnly->isEnabled() && m_ffOnly->isChecked(),
        .squash = m_squash->isEnabled() && m_squash->isChecked(),
        .noCommit = m_noCommit->isEnabled() && m_noCommit->isChecked(),
        .fetchTags = m_fetchTags->checkState(),
        .prune = m_prune->checkState(),
        .depth = m_depthCheck->isEnabled() && m_depthCheck->isChecked() ? std::optional(m_depth->value()) : std::nullopt,
    };
    const QString remoteBranch = m_remoteBranch->currentText().trimmed();
    if (m_remoteRadio->isChecked()) {
        chosen.remoteUrl = m_remote->currentText();
        chosen.allRemotes = m_remote->currentIndex() == 0 && m_remote->count() > 1 && !m_isPull;
        // When fetching, or pulling from the configured tracking branch, the remote branch isn't named: otherwise
        // git doesn't update the remote tracking branches
        if (!m_isPull || (m_configPullRemote == chosen.remoteUrl && m_configPullBranch == remoteBranch)) {
            chosen.remoteBranchName.clear();
        } else {
            chosen.remoteBranchName = remoteBranch;
        }
    } else {
        chosen.remoteUrl = m_other->currentText().trimmed();
        chosen.remoteBranchName = remoteBranch;
    }
    return chosen;
}

void PullFetchDlg::accept()
{
    const PullFetchOptions chosen = options();
    if (m_otherRadio->isChecked()) {
        // Only store the URL in the history if it was used
        saveHistory(m_other, pullUrls);
    }

    if (chosen.rebase && m_remoteBranch->currentText().trimmed().isEmpty() && m_isPull) {
        KMessageBox::error(this, i18n("You asked to pull, but did not specify a remote branch."), QString(), KMessageBox::Options());
        return;
    }

    KConfigGroup remoteHistory = historyGroup(QStringLiteral("PullRemote"));
    remoteHistory.writeEntry(m_git.workingDirectory(), m_remote->currentText());
    saveHistory(m_remoteBranch, pullRemoteBranches);
    KConfigGroup settings = KSharedConfig::openConfig()->group(settingsGroup());
    settings.writeEntry("ffonly", m_ffOnly->isChecked());
    settings.sync();

    QDialog::accept();
}

void PullFetchDlg::refresh()
{
    // Select the pull remote from the current branch; a detached HEAD has none
    QString currentBranch = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(m_git.workingDirectory()));
    if (currentBranch == QLatin1String("HEAD")) {
        currentBranch.clear();
    }

    // CGit::GetRemoteTrackedBranch
    QString pullRemote;
    QString pullBranch;
    if (!currentBranch.isEmpty()) {
        pullRemote = configValue(QStringLiteral("branch.%1.remote").arg(currentBranch));
        pullBranch = Git::stripRefName(configValue(QStringLiteral("branch.%1.merge").arg(currentBranch)));
    }
    m_configPullRemote = pullRemote;
    m_configPullBranch = pullBranch;

    // Without an upstream, the branch of the same name
    addString(m_remoteBranch, pullBranch.isEmpty() ? currentBranch : pullBranch);

    if (pullRemote.isEmpty()) {
        pullRemote = historyGroup(QStringLiteral("PullRemote")).readEntry(m_git.workingDirectory(), QString());
    }

    const QStringList remotes = QString::fromUtf8(m_git.run({QStringLiteral("remote")}, RunMode::ReadOnly)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const bool offerAll = !m_isPull && remotes.size() > 1;
    const QSignalBlocker blocker(m_remote);
    m_remote->clear();
    if (offerAll) {
        m_remote->addItem(allRemotesText());
    }
    m_remote->addItems(remotes);
    const int index = m_remote->findText(pullRemote, Qt::MatchExactly | Qt::MatchCaseSensitive);
    // With several remotes and none configured, fetching defaults to "- all -"
    m_remote->setCurrentIndex(std::max(index, 0));
    onRemoteChanged();
}

void PullFetchDlg::onRemoteChanged()
{
    const QString remote = m_remote->currentText();
    if (remote.isEmpty() || (m_remote->currentIndex() == 0 && m_remote->count() > 1 && !m_isPull)) {
        m_tagOpt->clear();
        m_pruneDefault->clear();
        return;
    }

    const QString tagopt = configValue(QStringLiteral("remote.%1.tagopt").arg(remote));
    QString tagDefault;
    if (tagopt == QLatin1String("--no-tags")) {
        tagDefault = i18n("None");
    } else if (tagopt == QLatin1String("--tags")) {
        tagDefault = i18n("All");
    } else {
        tagDefault = i18n("Reachable");
    }
    m_tagOpt->setText(i18nc("%1 is the default", "Default: %1", tagDefault));

    QString prune = configValue(QStringLiteral("remote.%1.prune").arg(remote));
    if (prune.isEmpty()) {
        prune = configValue(QStringLiteral("fetch.prune"));
    }
    m_pruneDefault->setText(prune.isEmpty() ? QString() : i18nc("%1 is the default", "Default: %1", prune));
}

void PullFetchDlg::onSourceChanged()
{
    if (m_remoteRadio->isChecked()) {
        m_remote->setEnabled(true);
        m_other->setEnabled(false);
        // TortoiseGit enables the rebase option here (KORTOISEGIT-0018)
        if (!m_isPull) {
            m_remoteBranch->setEnabled(false);
        }
        return;
    }

    // Similar code in PushDlg::OnBnClickedRd: a "git pull <url> <branch>" on the clipboard fills both fields
    QString clipPath = AppUtils::getClipboardLink(m_isPull ? QStringLiteral("git pull") : QStringLiteral("git fetch"), 1);
    if (clipPath.isEmpty()) {
        clipPath = AppUtils::getClipboardLink(m_isPull ? QStringLiteral("git fetch") : QStringLiteral("git pull"), 1);
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
        if (m_other->count() > 0) {
            m_other->setCurrentIndex(0);
        }
    } else if (const qsizetype separator = clipPath.indexOf(QLatin1Char(' ')); separator > 1 && clipPath.size() > separator + 2) {
        m_other->setEditText(unquote(clipPath.left(separator)));
        m_remoteBranch->setEditText(unquote(clipPath.mid(separator + 1)));
    } else {
        m_other->setEditText(clipPath);
    }
    m_remote->setEnabled(false);
    m_other->setEnabled(true);
    m_rebase->setChecked(false);
    m_remoteBranch->setEnabled(true);
}

void PullFetchDlg::updateFastForwardStates()
{
    if (!m_isPull) {
        return;
    }
    if (m_noFF->isChecked()) {
        m_ffOnly->setChecked(false);
    }
    m_ffOnly->setEnabled(!m_noFF->isChecked());
    if (m_ffOnly->isChecked()) {
        m_noFF->setChecked(false);
    }
    m_noFF->setEnabled(!m_ffOnly->isChecked());
}

QString PullFetchDlg::configValue(const QString& name) const
{
    try {
        return QString::fromUtf8(m_git.run({QStringLiteral("config"), QStringLiteral("--get"), name}, RunMode::ReadOnly)).trimmed();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: not set
        return {};
    }
}

QString PullFetchDlg::settingsGroup() const
{
    // TortoiseProc\PullFetch\<dir>_<m_IsPull>; TortoiseGit replaces the drive's ':', which Linux paths don't have
    return QStringLiteral("PullFetch %1_%2").arg(m_git.workingDirectory(), m_isPull ? QStringLiteral("1") : QStringLiteral("0"));
}

} // namespace kgt
