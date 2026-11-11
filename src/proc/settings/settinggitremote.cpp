// SPDX-FileCopyrightText: 2008-2021, 2023-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Settings/SettingGitRemote.cpp and its IDD_SETTINREMOTE resource.
 */

#include "settings/settinggitremote.hpp"

#include "apputils.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <chrono>
#include <tuple>

namespace
{

using namespace kgt;
using namespace std::chrono_literals;

// Like TortoiseGit's message boxes, these don't also raise a desktop notification
constexpr KMessageBox::Options noNotify = KMessageBox::Options();

// TIMER_PREFILL: how long after the page is created the hint about "origin" appears
constexpr auto prefillHintDelay = 1s;

// The tag option combo's entries, in TortoiseGit's order
constexpr int tagOptReachable = 0;
constexpr int tagOptNone = 1; // --no-tags
constexpr int tagOptAll = 2; // --tags

/**
 * @param parent The page.
 * @param question A question.
 * @param defaultNo Whether No is the default button, MB_DEFBUTTON2.
 * @param dontAskAgainName TortoiseGit's name of the "Don't show this message again" setting; empty for none.
 * @returns Whether the user answered Yes.
 */
bool askYesNo(QWidget *parent, const QString& question, bool defaultNo = false, const QString& dontAskAgainName = {})
{
    const auto answer = KMessageBox::questionTwoActions(parent,
                                                        question,
                                                        QString(),
                                                        KGuiItem(i18n("&Yes")),
                                                        KGuiItem(i18n("&No")),
                                                        dontAskAgainName,
                                                        defaultNo ? KMessageBox::Dangerous : noNotify);
    return answer == KMessageBox::PrimaryAction;
}

/**
 * @param setting A git configuration key.
 * @returns IDS_GITCONFIG_SETTING for it.
 */
QString settingToolTip(const QString& setting)
{
    return i18n("This option represents \"%1\" git config setting.", setting);
}

/**
 * @param remote A remote's name.
 * @param key One of its settings, such as "url".
 * @returns The configuration key.
 */
QString remoteKey(const QString& remote, QLatin1StringView key)
{
    return QStringLiteral("remote.%1.%2").arg(remote, key);
}

} // namespace

namespace kgt
{

SettingGitRemote::SettingGitRemote(const QString& repository, QWidget *parent)
    : SettingsPropPage(parent)
    , m_git(repository)
    , m_remoteList(new QListWidget(this))
    , m_remote(new QLineEdit(this))
    , m_url(new QLineEdit(this))
    , m_pushUrl(new QLineEdit(this))
    , m_tagOpt(new QComboBox(this))
    , m_pushDefault(new QCheckBox(i18n("Push Default"), this))
    , m_prune(new QCheckBox(i18n("Prune"), this))
    , m_addButton(new QPushButton(i18n("&Add New/Save"), this))
    , m_removeButton(new QPushButton(i18n("&Remove"), this))
    , m_renameButton(new QPushButton(i18n("Rename"), this))
{
    auto *layout = new QHBoxLayout(this);

    auto *listLayout = new QVBoxLayout;
    listLayout->addWidget(new QLabel(i18n("Remote:"), this));
    m_remoteList->setSortingEnabled(true); // LBS_SORT
    listLayout->addWidget(m_remoteList);
    layout->addLayout(listLayout, 1);

    auto *fields = new QGridLayout;
    const auto addLabel = [this, fields](int row, const QString& text) {
        fields->addWidget(new QLabel(text, this), row, 0);
    };
    addLabel(0, i18n("Remote:"));
    fields->addWidget(m_remote, 0, 1);
    fields->addWidget(m_renameButton, 0, 2);
    addLabel(1, i18n("URL:"));
    fields->addWidget(m_url, 1, 1, 1, 2);
    addLabel(2, i18n("Push URL:"));
    fields->addWidget(m_pushUrl, 2, 1, 1, 2);
    addLabel(3, i18n("Tags:"));
    fields->addWidget(m_tagOpt, 3, 1);
    fields->addWidget(m_pushDefault, 3, 2);
    fields->addWidget(m_prune, 4, 0, 1, 3);
    fields->setColumnStretch(1, 1);
    auto *fieldLayout = new QVBoxLayout;
    fieldLayout->addLayout(fields);
    fieldLayout->addSpacing(fieldLayout->spacing() * 2);
    fieldLayout->addWidget(m_addButton, 0, Qt::AlignLeft);
    fieldLayout->addWidget(m_removeButton, 0, Qt::AlignLeft);
    fieldLayout->addStretch();
    layout->addLayout(fieldLayout, 3);

    // OnInitDialog
    try {
        m_remoteList->addItems(QString::fromUtf8(m_git.run({QStringLiteral("remote")}, RunMode::ReadOnly)).split(QLatin1Char('\n'), Qt::SkipEmptyParts));
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Queued: the page isn't shown yet
        QTimer::singleShot(0, this, [this, message = e.error().message] {
            KMessageBox::error(this, i18n("Could not load remotes.") + QLatin1Char('\n') + message, QString(), noNotify);
        });
    }

    m_tagOpt->addItems({i18n("Reachable"), i18n("None"), i18n("All")});
    m_prune->setTristate(true);
    m_prune->setCheckState(Qt::PartiallyChecked);
    m_renameButton->setEnabled(false);

    m_pushDefault->setToolTip(settingToolTip(QStringLiteral("remote.pushdefault")));
    m_prune->setToolTip(settingToolTip(QStringLiteral("remote.<name>.prune")) + QLatin1Char('\n')
                        + i18n("You can configure it for all remotes or global on the Git settings page."));
    m_tagOpt->setToolTip(settingToolTip(QStringLiteral("remote.<name>.tagopt")));

    connect(m_addButton, &QPushButton::clicked, this, &SettingGitRemote::onButtonAdd);
    connect(m_removeButton, &QPushButton::clicked, this, &SettingGitRemote::onButtonRemove);
    connect(m_renameButton, &QPushButton::clicked, this, &SettingGitRemote::onButtonRenameRemote);
    connect(m_remoteList, &QListWidget::currentRowChanged, this, &SettingGitRemote::onSelChangeListRemote);
    // Only what the user does counts as a change, not a remote being loaded into the fields
    connect(m_remote, &QLineEdit::textEdited, this, &SettingGitRemote::onChangeEditRemote);
    connect(m_url, &QLineEdit::textEdited, this, &SettingGitRemote::onChangeEditUrl);
    connect(m_pushUrl, &QLineEdit::textEdited, this, [this] {
        onChange(RemotePushUrl);
    });
    connect(m_tagOpt, &QComboBox::activated, this, [this] {
        onChange(RemoteTagOpt);
    });
    connect(m_prune, &QCheckBox::clicked, this, [this] {
        onChange(RemotePrune);
    });
    connect(m_pushDefault, &QCheckBox::clicked, this, [this] {
        onChange(RemotePushDefault);
    });

    // OnTimer's TIMER_PREFILL
    QTimer::singleShot(prefillHintDelay, this, [this] {
        if (isVisible() && m_remote->text().isEmpty() && m_remoteList->count() == 0) {
            showEditBalloon(m_url,
                            i18n("When you type the URL, the remote name will be automatically filled with \"origin\" if the remote name is empty, "
                                 "so you do not have to."));
        }
    });
}

void SettingGitRemote::setNoFetch(bool noFetch)
{
    m_noFetch = noFetch;
}

bool SettingGitRemote::apply()
{
    const QString remote = m_remote->text().trimmed();

    if ((m_changedMask & RemotePushDefault) != 0) {
        if (!applyPushDefault(remote)) {
            return false;
        }
        m_changedMask &= ~RemotePushDefault;
    }

    if (m_changedMask != 0 && remote.isEmpty()) {
        KMessageBox::error(this, i18n("Remote name must not be empty."), QString(), noNotify);
        return false;
    }

    if ((m_changedMask & RemoteName) != 0) {
        if (!addRemote(remote, m_url->text())) {
            return false;
        }
        m_changedMask &= ~RemoteUrl;
    }
    if (!saveRemoteSettings(remote)) {
        return false;
    }

    setModified(false);
    m_changedMask = 0;
    return true;
}

bool SettingGitRemote::saveRemoteSettings(const QString& remote)
{
    if ((m_changedMask & RemoteUrl) != 0) {
        if (!saveGeneral(remoteKey(remote, QLatin1StringView("url")), m_url->text().replace(QLatin1Char('\\'), QLatin1Char('/')))) {
            return false;
        }
    }
    if ((m_changedMask & RemoteTagOpt) != 0) {
        QString tagOpt;
        if (m_tagOpt->currentIndex() == tagOptNone) {
            tagOpt = QStringLiteral("--no-tags");
        } else if (m_tagOpt->currentIndex() == tagOptAll) {
            tagOpt = QStringLiteral("--tags");
        }
        if (!saveGeneral(remoteKey(remote, QLatin1StringView("tagopt")), tagOpt)) {
            return false;
        }
    }
    if ((m_changedMask & RemotePrune) != 0) {
        const Qt::CheckState prune = m_prune->checkState();
        const QString value = prune == Qt::Checked ? QStringLiteral("true") : prune == Qt::Unchecked ? QStringLiteral("false") : QString();
        if (!saveGeneral(remoteKey(remote, QLatin1StringView("prune")), value)) {
            return false;
        }
    }
    if ((m_changedMask & RemotePushUrl) != 0) {
        if (!saveGeneral(remoteKey(remote, QLatin1StringView("pushurl")), m_pushUrl->text().replace(QLatin1Char('\\'), QLatin1Char('/')))) {
            return false;
        }
    }

    return true;
}

bool SettingGitRemote::remoteCollidesWithRefspec(const QString& remote, const QByteArray& configEntries)
{
    const QString ownKey = remoteKey(remote, QLatin1StringView("fetch"));
    const QString match = QStringLiteral(":refs/remotes/") + remote;
    for (const QByteArray& entry : configEntries.split('\0')) {
        const qsizetype newline = entry.indexOf('\n');
        if (newline < 0) {
            continue;
        }
        const QString value = QString::fromUtf8(entry.mid(newline + 1));
        const qsizetype pos = value.indexOf(match);
        if (QString::fromUtf8(entry.left(newline)) == ownKey || pos < 0) {
            continue;
        }
        // refs/remotes/<remote> itself or something under it, not another remote whose name merely starts the same
        const qsizetype end = pos + match.size();
        if (end == value.size() || value.at(end) == QLatin1Char('/')) {
            return true;
        }
    }
    return false;
}

void SettingGitRemote::onButtonAdd()
{
    const QString remote = m_remote->text();
    if (remote.isEmpty()) {
        KMessageBox::error(this, i18n("Remote name must not be empty."), QString(), noNotify);
        return;
    }
    if (m_url->text().isEmpty()) {
        KMessageBox::error(this, i18n("Remote URL must not be empty."), QString(), noNotify);
        return;
    }

    m_changedMask = RemoteName | RemoteUrl | RemoteTagOpt | RemotePrune | RemotePushDefault | RemotePushUrl;
    if (isRemoteExist(remote)) {
        if (!askYesNo(this, i18n("The remote \"%1\" already exists.\nDo you want to overwrite it?", remote), true)) {
            return;
        }
        m_changedMask &= ~RemoteName;
    }

    std::ignore = apply();
}

void SettingGitRemote::onButtonRemove()
{
    const QListWidgetItem *item = m_remoteList->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString remote = item->text();
    if (!askYesNo(this, i18n("Do you really want to remove \"%1\"?", remote))) {
        return;
    }
    try {
        std::ignore = m_git.run({QStringLiteral("remote"), QStringLiteral("rm"), QStringLiteral("--"), remote});
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, e.error().message, QString(), noNotify);
        return;
    }
    // CleanupSyncRemotes clears the Sync dialog's history of the remote, which we don't have yet
    {
        const QSignalBlocker blocker(m_remoteList);
        delete m_remoteList->takeItem(m_remoteList->row(item)); // NOLINT(cppcoreguidelines-owning-memory): takeItem hands the item over
        m_remoteList->setCurrentRow(-1);
    }
    onSelChangeListRemote();
}

void SettingGitRemote::onButtonRenameRemote()
{
    QListWidgetItem *item = m_remoteList->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString newRemote = m_remote->text();
    try {
        std::ignore = m_git.run({QStringLiteral("remote"), QStringLiteral("rename"), QStringLiteral("--"), item->text(), newRemote});
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, e.error().message, QString(), noNotify);
        return;
    }
    {
        // The sorted list moves the item to its new place
        const QSignalBlocker blocker(m_remoteList);
        item->setText(newRemote);
        m_remoteList->setCurrentItem(item);
    }
    m_changedMask &= ~RemoteName;
    if (m_changedMask == 0) {
        setModified(false);
    }
}

void SettingGitRemote::onSelChangeListRemote()
{
    if (m_changedMask != 0) {
        const auto answer = KMessageBox::questionTwoActions(this,
                                                            i18n("The Remote Config was changed.\nDo you want to save now or discard changes?"),
                                                            QString(),
                                                            KGuiItem(i18n("&Save")),
                                                            KGuiItem(i18n("&Discard")),
                                                            QString(),
                                                            noNotify);
        if (answer == KMessageBox::PrimaryAction) {
            std::ignore = apply();
        }
    }
    setModified(false);

    const QListWidgetItem *item = m_remoteList->currentItem();
    if (item == nullptr) {
        m_url->clear();
        m_pushUrl->clear();
        m_remote->clear();
        return;
    }
    const QString remote = item->text();
    m_remote->setText(remote);
    m_url->setText(m_git.configValue(remoteKey(remote, QLatin1StringView("url"))));
    m_pushUrl->setText(m_git.configValue(remoteKey(remote, QLatin1StringView("pushurl"))));
    m_changedMask = 0;

    const QString tagOpt = m_git.configValue(remoteKey(remote, QLatin1StringView("tagopt")));
    int index = tagOptReachable;
    if (tagOpt == QLatin1String("--no-tags")) {
        index = tagOptNone;
    } else if (tagOpt == QLatin1String("--tags")) {
        index = tagOptAll;
    }
    m_tagOpt->setCurrentIndex(index);

    m_pushDefault->setChecked(m_git.configValue(QStringLiteral("remote.pushdefault")) == remote);
    const QString prune = m_git.configValue(remoteKey(remote, QLatin1StringView("prune")));
    m_prune->setCheckState(prune == QLatin1String("true") ? Qt::Checked : prune == QLatin1String("false") ? Qt::Unchecked : Qt::PartiallyChecked);

    m_addButton->setEnabled(true);
    m_removeButton->setEnabled(true);
    m_renameButton->setEnabled(true);
}

void SettingGitRemote::onChangeEditRemote()
{
    m_changedMask |= RemoteName;
    if (isRemoteCollideWithRefspec(m_remote->text())) {
        showEditBalloon(m_remote, i18n("This remote name collides with fetch refspec of other remotes\nPlease use another name"));
    }
    if (!m_remote->text().isEmpty() && !m_url->text().isEmpty()) {
        setModified();
    }
}

void SettingGitRemote::onChangeEditUrl()
{
    m_changedMask |= RemoteUrl;
    if (m_remote->text().isEmpty() && !m_url->text().isEmpty() && m_remoteList->count() == 0) {
        m_remote->setText(QStringLiteral("origin"));
        onChangeEditRemote();
    }
    if (!m_remote->text().isEmpty() && !m_url->text().isEmpty()) {
        setModified();
    }
}

void SettingGitRemote::onChange(ChangedFlag flag)
{
    m_changedMask |= flag;
    // OnEnChangeEditPushUrl waits for a remote name; the tag option, prune and push default don't
    if (flag != RemotePushUrl || !m_remote->text().isEmpty()) {
        setModified();
    }
}

bool SettingGitRemote::addRemote(const QString& remote, const QString& url)
{
    if (url.isEmpty()) {
        KMessageBox::error(this, i18n("Remote URL must not be empty."), QString(), noNotify);
        return false;
    }

    // With another remote already there, this one is probably not the official one: don't fetch its tags
    if (m_remoteList->count() > 0 && m_tagOpt->currentIndex() != tagOptNone
        && askYesNo(this,
                    i18n("To avoid fetching wrong tags, if this is not an official remote,\nyou are advised to disable tag fetching for this "
                         "remote.\nDisable tag fetching?"),
                    false,
                    QStringLiteral("TagOptNoTagsWarning"))) {
        m_tagOpt->setCurrentIndex(tagOptNone);
        m_changedMask |= RemoteTagOpt;
    }

    try {
        std::ignore = m_git.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("--"), remote, url});
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, e.error().message, QString(), noNotify);
        return false;
    }

    addAndSelect(remote);
    m_addButton->setEnabled(true);
    m_renameButton->setEnabled(true);
    if (!m_noFetch && askYesNo(this, i18n("Do you want to fetch remote branches from the newly added remote?"))) {
        AppUtils::runTortoiseGitProc({
            QStringLiteral("--command"),
            QStringLiteral("fetch"),
            QStringLiteral("--path"),
            m_git.workingDirectory(),
            QStringLiteral("--remote"),
            remote,
        });
    }
    return true;
}

bool SettingGitRemote::applyPushDefault(const QString& remote)
{
    const QString key = QStringLiteral("remote.pushdefault");
    if (!remote.isEmpty() && m_pushDefault->isChecked() && !saveGeneral(key, remote)) {
        return false;
    }
    return m_pushDefault->isChecked() || remote != m_git.configValue(key) || saveGeneral(key, QString());
}

bool SettingGitRemote::isRemoteExist(const QString& remote) const
{
    return !m_remoteList->findItems(remote, Qt::MatchExactly | Qt::MatchCaseSensitive).isEmpty();
}

bool SettingGitRemote::isRemoteCollideWithRefspec(const QString& remote) const
{
    try {
        // The keys TortoiseGit goes through: every remote's and git-svn remote's refspecs
        const QString pattern = QStringLiteral("^(remote\\..*\\.fetch|svn-remote\\..*\\.(fetch|branches|tags))$");
        const QStringList arguments{QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("-z"), QStringLiteral("--get-regexp"), pattern};
        return remoteCollidesWithRefspec(remote, m_git.run(arguments, RunMode::ReadOnly));
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: no remote has a refspec yet
        return false;
    }
}

bool SettingGitRemote::saveGeneral(const QString& key, const QString& value)
{
    const auto showFailure = [this, &key, &value] {
        KMessageBox::error(this, i18n("Saving config failed (key: \"%1\", value: \"%2\").", key, value), QString(), noNotify);
    };
    try {
        if (!value.isEmpty()) {
            m_git.setConfigValue(key, value);
            return true;
        }
        if (m_git.configValueAt(key, ConfigLevel::Local)) {
            m_git.unsetConfigValue(key);
        }
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        showFailure();
        return false;
    }
    // TortoiseGit doesn't check the unset's result, which fails if the key isn't set; it looks whether a value is left
    if (!m_git.configValue(key).isEmpty()) {
        showFailure();
        return false;
    }
    return true;
}

void SettingGitRemote::addAndSelect(const QString& remote)
{
    const QSignalBlocker blocker(m_remoteList);
    auto *item = new QListWidgetItem(remote, m_remoteList);
    m_remoteList->setCurrentItem(item);
}

void SettingGitRemote::showEditBalloon(QLineEdit *edit, const QString& text)
{
    QToolTip::showText(edit->mapToGlobal(QPoint(0, edit->height())), text, edit);
}

} // namespace kgt
