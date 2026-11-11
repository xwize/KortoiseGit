// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "settings/settings.hpp"
#include "commands/pushcommand.hpp"
#include "commands/settingscommand.hpp"
#include "pushdlg.hpp"
#include "settings/settinggitremote.hpp"
#include "settings/settingsprogsdiff.hpp"
#include "testrepo.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QPixmap>
#include <QRadioButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <tuple>

namespace
{

using namespace kgt;

/**
 * @param entries Key and value pairs.
 * @returns What git config -z --get-regexp prints for them.
 */
QByteArray configOutput(const QList<std::pair<QByteArray, QByteArray>>& entries)
{
    QByteArray output;
    for (const auto& [key, value] : entries) {
        output += key + '\n' + value + '\0';
    }
    return output;
}

/**
 * Saves a screenshot of a dialog if KGT_SCREENSHOT_DIR is set.
 * @param dialog The dialog.
 * @param name The file's name.
 */
void screenshot(QWidget& dialog, const QString& name)
{
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    const QPixmap image = dialog.grab();
    QVERIFY(!image.isNull());
    const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
    if (!screenshotDir.isEmpty()) {
        QVERIFY(image.save(QDir(screenshotDir).absoluteFilePath(name)));
    }
}

} // namespace

/**
 * The Settings dialog's pages, the Remote page's refspec check and Push's arbitrary URL.
 */
class SettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Histories and stored decisions go to a test configuration, not the user's
        QStandardPaths::setTestModeEnabled(true);
        TestRepo::isolateGit();
    }

    void refspecCollision()
    {
        const QByteArray config = configOutput({
            {"remote.origin.fetch", "+refs/heads/*:refs/remotes/origin/*"},
            {"remote.mirror.fetch", "+refs/heads/*:refs/remotes/upstream/*"},
            {"svn-remote.svn.branches", "branches/*:refs/remotes/svn/*"},
        });
        // Its own refspec doesn't count
        QVERIFY(!SettingGitRemote::remoteCollidesWithRefspec(QStringLiteral("origin"), config));
        // Another remote writes to refs/remotes/upstream/
        QVERIFY(SettingGitRemote::remoteCollidesWithRefspec(QStringLiteral("upstream"), config));
        QVERIFY(SettingGitRemote::remoteCollidesWithRefspec(QStringLiteral("svn"), config));
        // A name that only starts like another doesn't collide
        QVERIFY(!SettingGitRemote::remoteCollidesWithRefspec(QStringLiteral("up"), config));
        QVERIFY(!SettingGitRemote::remoteCollidesWithRefspec(QStringLiteral("new"), config));
        // A refspec ending at the remote's folder itself
        QVERIFY(SettingGitRemote::remoteCollidesWithRefspec(QStringLiteral("one"), configOutput({{"remote.x.fetch", "refs/heads/main:refs/remotes/one"}})));
        QVERIFY(!SettingGitRemote::remoteCollidesWithRefspec(QStringLiteral("x"), QByteArray()));
    }

    void pagesInsideRepository()
    {
        const TestRepo repo;
        QCOMPARE(SettingsCommand::repository(repo.path()), repo.path());
        Settings dlg(repo.path());
        QCOMPARE(
            dlg.pageNames(),
            (QStringList{QStringLiteral("gitconfig"), QStringLiteral("gitremote"), QStringLiteral("diff"), QStringLiteral("merge"), QStringLiteral("save")}));
        dlg.setDefaultPage(QStringLiteral("gitremote"));
        QCOMPARE(dlg.currentPageName(), QStringLiteral("gitremote"));
        // An unknown page leaves the dialog where it is
        dlg.setDefaultPage(QStringLiteral("overlay"));
        QCOMPARE(dlg.currentPageName(), QStringLiteral("gitremote"));
        screenshot(dlg, QStringLiteral("settings-remote.png"));
    }

    void newRepositoryGetsRemoteAndPushes()
    {
        // A repository as Create repository makes it, with a commit and no remote
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        const QTemporaryDir server;
        repo.run({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--bare"), server.path()});

        // Manage Remotes: the Remote page, where typing the first URL names the remote "origin"
        Settings dlg(repo.path());
        dlg.setDefaultPage(QStringLiteral("gitremote"));
        const QList<QLineEdit *> edits = dlg.page(QStringLiteral("gitremote"))->findChildren<QLineEdit *>();
        QCOMPARE(edits.size(), 3); // Remote, URL, Push URL
        QTest::keyClicks(edits.at(1), server.path());
        QCOMPARE(edits.at(0)->text(), QStringLiteral("origin"));
        dlg.accept();
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        const Git git(repo.path());
        QCOMPARE(git.configValue(QStringLiteral("remote.origin.url")), server.path());

        // Push now offers it, and pushing tracks the branch
        const PushOptions options = PushDlg(repo.path()).options();
        QCOMPARE(options.remote, QStringLiteral("origin"));
        QVERIFY(options.setUpstream);
        std::ignore = git.run(PushCommand::pushArguments(options));
        QCOMPARE(Git(server.path()).hash(QStringLiteral("main")), git.hash(QStringLiteral("HEAD")));
        QCOMPARE(git.configValue(QStringLiteral("branch.main.remote")), QStringLiteral("origin"));
    }

    void pagesOutsideRepository()
    {
        const QTemporaryDir plain;
        QVERIFY(SettingsCommand::repository(plain.path()).isEmpty());
        const Settings dlg(QString{});
        QCOMPARE(dlg.pageNames(), (QStringList{QStringLiteral("gitconfig"), QStringLiteral("diff"), QStringLiteral("merge"), QStringLiteral("save")}));
        QVERIFY(dlg.page(QStringLiteral("gitremote")) == nullptr);
    }

    void diffToolIsSavedGlobally()
    {
        const QTemporaryDir home;
        const QString globalConfig = QDir(home.path()).filePath(QStringLiteral("gitconfig"));
        qputenv("GIT_CONFIG_GLOBAL", QFile::encodeName(globalConfig));
        const auto restore = qScopeGuard([] {
            qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        });
        const Git git(home.path());

        SettingsProgsDiff page;
        QVERIFY(page.tool().isEmpty());
        page.setTool(QStringLiteral("meld"));
        QVERIFY(page.apply());
        QCOMPARE(git.configValueAt(QStringLiteral("diff.tool"), ConfigLevel::Global), QStringLiteral("meld"));

        // A tool set outside the dialog is shown as it is; "Default" removes the setting
        git.setConfigValue(QStringLiteral("diff.tool"), QStringLiteral("vimdiff"), ConfigLevel::Global);
        SettingsProgsDiff again;
        QCOMPARE(again.tool(), QStringLiteral("vimdiff"));
        again.setTool(QString());
        QVERIFY(again.apply());
        QVERIFY(!git.configValueAt(QStringLiteral("diff.tool"), ConfigLevel::Global));
    }

    void pushToArbitraryUrl()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        repo.commitAll(QStringLiteral("initial"));
        repo.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), repo.path()});

        PushDlg dlg(repo.path());
        const QCheckBox *setUpstream = nullptr;
        const QList<QCheckBox *> boxes = dlg.findChildren<QCheckBox *>();
        for (const QCheckBox *box : boxes) {
            if (box->text().contains(QLatin1String("upstream"))) {
                setUpstream = box;
            }
        }
        QVERIFY(setUpstream != nullptr && setUpstream->isEnabled());

        auto *urlRadio = dlg.findChild<QRadioButton *>(QStringLiteral("urlRadio"));
        auto *remoteUrl = dlg.findChild<QComboBox *>(QStringLiteral("remoteUrl"));
        QVERIFY(urlRadio != nullptr && remoteUrl != nullptr);
        QVERIFY(!remoteUrl->isEnabled());
        urlRadio->click();
        QVERIFY(remoteUrl->isEnabled());
        remoteUrl->setEditText(QStringLiteral("/srv/git/project.git"));

        const PushOptions options = dlg.options();
        QCOMPARE(options.remote, QStringLiteral("/srv/git/project.git"));
        // EnDisablePushRemoteArchiveBranch: no upstream for a URL
        QVERIFY(!setUpstream->isEnabled());
        QVERIFY(!options.setUpstream);
        screenshot(dlg, QStringLiteral("pushdlg-url.png"));
    }
};

QTEST_MAIN(SettingsTest)

#include "settingstest.moc"
