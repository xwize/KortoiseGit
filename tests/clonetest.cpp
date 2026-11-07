// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apputils.hpp"
#include "clonedlg.hpp"
#include "commands/clonecommand.hpp"
#include "progressdlg.hpp"
#include "testrepo.hpp"

#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QLineEdit>
#include <QPixmap>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

namespace
{

using namespace kgt;

/**
 * @param dlg The dialog.
 * @param text A checkbox's text.
 * @returns The checkbox.
 */
QCheckBox *checkBox(const CloneDlg& dlg, const QString& text)
{
    for (QCheckBox *box : dlg.findChildren<QCheckBox *>()) {
        if (box->text() == text) {
            return box;
        }
    }
    return nullptr;
}

/**
 * Runs a clone in a ProgressDlg, as CloneCommand does after the dialog, and waits for it.
 * @param options What to clone where.
 * @returns git's status, or -1 if it didn't end in time.
 */
int runClone(const CloneOptions& options)
{
    ProgressDlg progress(CloneCommand::existingDirectoryForClone(options.directory));
    progress.addGitCmd(CloneCommand::cloneArguments(options));
    QSignalSpy finished(&progress, &ProgressDlg::commandsFinished);
    progress.start();
    if (!finished.wait(30000)) {
        return -1;
    }
    return progress.gitStatus();
}

/**
 * @param directory A repository.
 * @param arguments The arguments after "git".
 * @returns Git's trimmed output there.
 */
QString gitOutput(const QString& directory, const QStringList& arguments)
{
    return QString::fromUtf8(Git(directory).run(arguments)).trimmed();
}

/**
 * A repository with three commits on main and two more on branch feature, checked out on main.
 */
struct Source {
    TestRepo repo; // The repository to clone

    Source()
    {
        for (const char *const content : {"1\n", "2\n", "3\n"}) {
            repo.write(QStringLiteral("a.txt"), content);
            repo.commitAll(QStringLiteral("main"));
        }
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), QStringLiteral("feature")});
        for (const char *const content : {"4\n", "5\n"}) {
            repo.write(QStringLiteral("b.txt"), content);
            repo.commitAll(QStringLiteral("feature"));
        }
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
    }
};

} // namespace

/**
 * CloneDlg, CloneCommand's command line, and cloning a local repository through ProgressDlg.
 */
class CloneTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void init()
    {
        QGuiApplication::clipboard()->clear();
    }

    // NOLINTNEXTLINE(readability-identifier-naming): QtTest finds data functions by the _data suffix
    void moduleName_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("name");
        QTest::newRow("https") << QStringLiteral("https://github.com/user/repo.git") << QStringLiteral("repo");
        QTest::newRow("https without .git") << QStringLiteral("https://invent.kde.org/sdk/dolphin-plugins") << QStringLiteral("dolphin-plugins");
        QTest::newRow("trailing slash") << QStringLiteral("https://host/path/repo/") << QStringLiteral("repo");
        QTest::newRow("ssh") << QStringLiteral("ssh://git@host:2222/user/Repo.GIT") << QStringLiteral("Repo");
        QTest::newRow("git protocol") << QStringLiteral("git://host/project.git") << QStringLiteral("project");
        QTest::newRow("scp-like") << QStringLiteral("git@github.com:user/repo.git") << QStringLiteral("repo");
        QTest::newRow("scp-like without path") << QStringLiteral("user@host:repo.git") << QStringLiteral("repo");
        QTest::newRow("local path") << QStringLiteral("/home/me/src/project") << QStringLiteral("project");
        QTest::newRow("local bare") << QStringLiteral("/srv/git/project.git/") << QStringLiteral("project");
        QTest::newRow("file url") << QStringLiteral("file:///srv/git/tool.git") << QStringLiteral("tool");
        QTest::newRow("bare name") << QStringLiteral("repo.git") << QStringLiteral("repo");
        // Only a final .git goes
        QTest::newRow("inner .git") << QStringLiteral("https://host/a.github.io") << QStringLiteral("a.github.io");
    }

    void moduleName()
    {
        QFETCH(const QString, url);
        QFETCH(const QString, name);
        QCOMPARE(CloneDlg::moduleName(url), name);
    }

    void directoryFollowsUrl()
    {
        CloneDlg dlg(QStringLiteral("/tmp/parent"));
        QCOMPARE(dlg.options().directory, QStringLiteral("/tmp/parent"));
        dlg.setUrl(QStringLiteral("https://host/user/first.git"));
        QCOMPARE(dlg.options().directory, QStringLiteral("/tmp/parent/first"));
        // The name from the previous URL is replaced
        dlg.setUrl(QStringLiteral("git@host:user/second.git"));
        QCOMPARE(dlg.options().directory, QStringLiteral("/tmp/parent/second"));

        // A directory the user typed keeps its folder and gets the new name added
        auto *directory = dlg.findChild<QLineEdit *>(QStringLiteral("directory"));
        QVERIFY(directory);
        directory->setText(QStringLiteral("/other/place/"));
        dlg.setUrl(QStringLiteral("/srv/third"));
        QCOMPARE(dlg.options().directory, QStringLiteral("/other/place/third"));
    }

    void clipboardLink()
    {
        const QString prefix = QStringLiteral("git clone ");
        QCOMPARE(AppUtils::clipboardLink(QStringLiteral("https://host/repo.git"), prefix), QStringLiteral("https://host/repo.git"));
        QCOMPARE(AppUtils::clipboardLink(QStringLiteral("git@host:repo.git\nsecond line"), prefix), QStringLiteral("git@host:repo.git"));
        QCOMPARE(AppUtils::clipboardLink(QStringLiteral("\"ssh://host/repo\""), prefix), QStringLiteral("ssh://host/repo"));
        QCOMPARE(AppUtils::clipboardLink(QStringLiteral("git clone https://host/repo.git target"), prefix), QStringLiteral("https://host/repo.git"));
        QCOMPARE(AppUtils::clipboardLink(QStringLiteral("git clone user@host:repo.git"), prefix), QStringLiteral("user@host:repo.git"));
        QCOMPARE(AppUtils::clipboardLink(QStringLiteral("/srv/git/repo.git"), prefix), QStringLiteral("/srv/git/repo.git"));
        QVERIFY(AppUtils::clipboardLink(QStringLiteral("just some text"), prefix).isEmpty());
        QVERIFY(AppUtils::clipboardLink(QStringLiteral("https://"), prefix).isEmpty());
        QVERIFY(AppUtils::clipboardLink(QString(), prefix).isEmpty());
    }

    void clipboardPrefillsUrl()
    {
        QGuiApplication::clipboard()->setText(QStringLiteral("git clone https://host/user/project.git"));
        const CloneDlg dlg(QStringLiteral("/tmp/parent"));
        QCOMPARE(dlg.options().url, QStringLiteral("https://host/user/project.git"));
        QCOMPARE(dlg.options().directory, QStringLiteral("/tmp/parent/project"));

        QGuiApplication::clipboard()->setText(QStringLiteral("not a url"));
        QVERIFY(CloneDlg(QStringLiteral("/tmp/parent")).options().url.isEmpty());
    }

    void cloneArguments()
    {
        CloneOptions options;
        options.url = QStringLiteral("https://host/repo.git");
        options.directory = QStringLiteral("/tmp/repo");
        QCOMPARE(CloneCommand::cloneArguments(options),
                 (QStringList{QStringLiteral("clone"),
                              QStringLiteral("--progress"),
                              QStringLiteral("-v"),
                              QStringLiteral("--"),
                              QStringLiteral("https://host/repo.git"),
                              QStringLiteral("/tmp/repo")}));
        options.recursive = true;
        options.noCheckout = true;
        options.branch = true;
        options.branchName = QStringLiteral("dev");
        options.origin = true;
        options.originName = QStringLiteral("upstream");
        options.depth = true;
        options.depthValue = 3;
        QCOMPARE(CloneCommand::cloneArguments(options),
                 (QStringList{QStringLiteral("clone"),
                              QStringLiteral("--progress"),
                              QStringLiteral("--recursive"),
                              QStringLiteral("--no-checkout"),
                              QStringLiteral("--branch"),
                              QStringLiteral("dev"),
                              QStringLiteral("--origin"),
                              QStringLiteral("upstream"),
                              QStringLiteral("--depth"),
                              QStringLiteral("3"),
                              QStringLiteral("-v"),
                              QStringLiteral("--"),
                              QStringLiteral("https://host/repo.git"),
                              QStringLiteral("/tmp/repo")}));
    }

    void existingDirectoryForClone()
    {
        const QTemporaryDir parent;
        QCOMPARE(CloneCommand::existingDirectoryForClone(parent.path()), parent.path());
        QCOMPARE(CloneCommand::existingDirectoryForClone(parent.filePath(QStringLiteral("new/deeper"))), parent.path());
    }

    void bareExcludesOtherOptions()
    {
        const CloneDlg dlg(QStringLiteral("/tmp/parent"));
        QCheckBox *bare = checkBox(dlg, QStringLiteral("Clone into Bare Repo"));
        QCheckBox *recursive = checkBox(dlg, QStringLiteral("Recursive"));
        const QCheckBox *noCheckout = checkBox(dlg, QStringLiteral("No Checkout"));
        QCheckBox *origin = checkBox(dlg, QStringLiteral("Origin Name"));
        QVERIFY(bare && recursive && noCheckout && origin);
        bare->setChecked(true);
        QVERIFY(!recursive->isEnabled() && !noCheckout->isEnabled() && !origin->isEnabled());
        bare->setChecked(false);
        recursive->setChecked(true);
        QVERIFY(!bare->isEnabled());
        recursive->setChecked(false);
        origin->setChecked(true);
        QVERIFY(!bare->isEnabled());
    }

    void clonePlain()
    {
        const Source source;
        const QTemporaryDir parent;
        CloneDlg dlg(parent.path());
        dlg.setUrl(source.repo.path());
        const CloneOptions options = dlg.options();
        QCOMPARE(options.directory, parent.filePath(QFileInfo(source.repo.path()).fileName()));

        QCOMPARE(runClone(options), 0);
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")}), QStringLiteral("main"));
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}), QStringLiteral("3"));
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("remote")}), QStringLiteral("origin"));
        QVERIFY(QFile::exists(QDir(options.directory).filePath(QStringLiteral("a.txt"))));

        saveScreenshot(dlg);
    }

    void cloneBranchDepthOrigin()
    {
        const Source source;
        const QTemporaryDir parent;
        CloneDlg dlg(parent.path());
        // --depth only applies to a local repository given as a file:// URL
        dlg.setUrl(QStringLiteral("file://") + source.repo.path());
        checkBox(dlg, QStringLiteral("Branch"))->setChecked(true);
        dlg.findChild<QLineEdit *>(QStringLiteral("branchName"))->setText(QStringLiteral("feature"));
        checkBox(dlg, QStringLiteral("Depth"))->setChecked(true);
        dlg.findChild<QSpinBox *>(QStringLiteral("depthValue"))->setValue(2);
        checkBox(dlg, QStringLiteral("Origin Name"))->setChecked(true);
        dlg.findChild<QLineEdit *>(QStringLiteral("originName"))->setText(QStringLiteral("upstream"));
        const CloneOptions options = dlg.options();

        QCOMPARE(runClone(options), 0);
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")}),
                 QStringLiteral("feature"));
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}), QStringLiteral("2"));
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("rev-parse"), QStringLiteral("--is-shallow-repository")}), QStringLiteral("true"));
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("remote")}), QStringLiteral("upstream"));
    }

    void cloneBare()
    {
        const Source source;
        const QTemporaryDir parent;
        CloneDlg dlg(parent.path());
        dlg.setUrl(source.repo.path() + QStringLiteral("/"));
        checkBox(dlg, QStringLiteral("Clone into Bare Repo"))->setChecked(true);
        const CloneOptions options = dlg.options();

        QCOMPARE(runClone(options), 0);
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("rev-parse"), QStringLiteral("--is-bare-repository")}), QStringLiteral("true"));
        QCOMPARE(gitOutput(options.directory, {QStringLiteral("rev-parse"), QStringLiteral("feature")}),
                 gitOutput(source.repo.path(), {QStringLiteral("rev-parse"), QStringLiteral("feature")}));
        QVERIFY(!QFile::exists(QDir(options.directory).filePath(QStringLiteral("a.txt"))));
    }

    void cloneFailureIsReported()
    {
        const QTemporaryDir parent;
        CloneOptions options;
        options.url = parent.filePath(QStringLiteral("missing"));
        options.directory = parent.filePath(QStringLiteral("target"));
        QVERIFY(runClone(options) > 0);
        QVERIFY(!QFile::exists(options.directory));
    }

private:
    /**
     * @param dlg The dialog to save, if KGT_SCREENSHOT_DIR is set.
     */
    static void saveScreenshot(CloneDlg& dlg)
    {
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QPixmap screenshot = dlg.grab();
        QVERIFY(!screenshot.isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("clonedlg.png"))));
        }
    }
};

QTEST_MAIN(CloneTest)

#include "clonetest.moc"
