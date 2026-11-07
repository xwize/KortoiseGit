// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "commands/createrepositorycommand.hpp"
#include "createrepodlg.hpp"
#include "git/gitadmindir.hpp"
#include "testrepo.hpp"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace
{

using namespace kgt;

/**
 * Answers the modal dialogs that appear while it lives: CreateRepoDlg is accepted, and each message box is
 * answered with the button whose text is @p button (or accepted when empty). Their texts are kept.
 */
class ModalAnswerer
{
public:
    /**
     * @param button The message box button to click, such as "A&bort", or empty to accept message boxes.
     * @param bare Whether to ask CreateRepoDlg for a bare repository.
     */
    explicit ModalAnswerer(QString button = {}, bool bare = false)
        : m_button(std::move(button))
        , m_bare(bare)
    {
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] {
            answer();
        });
        m_timer.start(20);
    }

    /**
     * @returns The texts of the message boxes answered, in order.
     */
    [[nodiscard]] const QStringList& messages() const
    {
        return m_messages;
    }

private:
    void answer()
    {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        if (auto *createRepo = qobject_cast<CreateRepoDlg *>(dialog)) {
            createRepo->setBare(m_bare);
            createRepo->accept();
            return;
        }
        QString text;
        for (const QLabel *label : dialog->findChildren<QLabel *>()) {
            text += label->text();
        }
        m_messages.append(text);
        for (QPushButton *button : dialog->findChildren<QPushButton *>()) {
            if (!m_button.isEmpty() && button->text() == m_button) {
                button->click();
                return;
            }
        }
        dialog->accept();
    }

    QTimer m_timer;
    QString m_button;
    bool m_bare;
    QStringList m_messages;
};

/**
 * @param folder The folder to create a repository in.
 * @returns The command, ready to execute.
 */
std::unique_ptr<CreateRepositoryCommand> command(const QString& folder)
{
    auto created = std::make_unique<CreateRepositoryCommand>();
    created->setPaths({folder});
    return created;
}

} // namespace

/**
 * Creating repositories: the checks, git init, and the whole command with its dialogs answered.
 * Set KGT_SCREENSHOT_DIR to save a screenshot there.
 */
class CreateRepositoryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void initsNormalRepository()
    {
        const QTemporaryDir dir;
        const QString folder = dir.filePath(QStringLiteral("new/project"));
        CreateRepositoryCommand::initRepository(folder, false);
        QVERIFY(QFileInfo(QDir(folder).filePath(QStringLiteral(".git/HEAD"))).isFile());
        QVERIFY(!GitAdminDir::isBareRepo(folder));
        QCOMPARE(CreateRepositoryCommand::existingRepository(folder), QDir(folder).filePath(QStringLiteral(".git")));
    }

    void initsBareRepository()
    {
        const QTemporaryDir dir;
        const QString folder = dir.filePath(QStringLiteral("project.git"));
        CreateRepositoryCommand::initRepository(folder, true);
        QVERIFY(GitAdminDir::isBareRepo(folder));
        QVERIFY(!QFileInfo::exists(QDir(folder).filePath(QStringLiteral(".git"))));
        QCOMPARE(CreateRepositoryCommand::existingRepository(folder), folder);
    }

    void findsExistingRepository()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("sub/a.txt"), "a\n");
        const QString adminDir = repo.filePath(QStringLiteral(".git"));
        QCOMPARE(CreateRepositoryCommand::existingRepository(repo.path()), adminDir);
        QCOMPARE(CreateRepositoryCommand::existingRepository(repo.filePath(QStringLiteral("sub"))), adminDir);
        QCOMPARE(CreateRepositoryCommand::existingRepository(repo.filePath(QStringLiteral("not/yet"))), adminDir);

        const QTemporaryDir plain;
        QVERIFY(CreateRepositoryCommand::existingRepository(plain.path()).isEmpty());
    }

    void recognisesSpecialFolders()
    {
        QVERIFY(CreateRepositoryCommand::isSpecialFolder(QDir::rootPath()));
        QVERIFY(CreateRepositoryCommand::isSpecialFolder(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)));
        const QTemporaryDir dir;
        QVERIFY(!CreateRepositoryCommand::isSpecialFolder(dir.path()));
        QVERIFY(!CreateRepositoryCommand::isSpecialFolder(dir.filePath(QStringLiteral("missing"))));
    }

    void bareByDefaultForDotGitName()
    {
        QVERIFY(CreateRepoDlg(QStringLiteral("/tmp/project.git")).isBare());
        QVERIFY(!CreateRepoDlg(QStringLiteral("/tmp/project")).isBare());
    }

    void commandCreatesRepository()
    {
        const QTemporaryDir dir;
        const ModalAnswerer answerer;
        QVERIFY(command(dir.path())->execute());
        QVERIFY(QFileInfo(dir.filePath(QStringLiteral(".git/HEAD"))).isFile());
        QCOMPARE(answerer.messages().size(), 1);
        QVERIFY2(answerer.messages().constFirst().contains(QStringLiteral("Initialized empty Git repository in %1.").arg(dir.path())),
                 qPrintable(answerer.messages().constFirst()));
    }

    void commandCreatesBareRepository()
    {
        const QTemporaryDir dir;
        const QString folder = dir.filePath(QStringLiteral("project.git"));
        const ModalAnswerer answerer({}, true);
        QVERIFY(command(folder)->execute());
        QVERIFY(GitAdminDir::isBareRepo(folder));
    }

    void commandWarnsInsideRepository()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("sub/a.txt"), "a\n");
        const QString folder = repo.filePath(QStringLiteral("sub"));
        const ModalAnswerer answerer(QStringLiteral("A&bort"));
        QVERIFY(!command(folder)->execute());
        QCOMPARE(answerer.messages().size(), 1);
        QVERIFY2(answerer.messages().constFirst().contains(QStringLiteral("is already inside the git repository")),
                 qPrintable(answerer.messages().constFirst()));
        QVERIFY(!QFileInfo::exists(QDir(folder).filePath(QStringLiteral(".git"))));
    }

    void commandWarnsBeforeBareInNonEmptyFolder()
    {
        const QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const ModalAnswerer answerer(QStringLiteral("A&bort"), true);
        QVERIFY(!command(dir.path())->execute());
        QCOMPARE(answerer.messages().size(), 1);
        QVERIFY2(answerer.messages().constFirst().contains(QStringLiteral("is not empty!")), qPrintable(answerer.messages().constFirst()));
        QVERIFY(!GitAdminDir::isBareRepo(dir.path()));
    }

    void screenshot()
    {
        CreateRepoDlg dlg(QStringLiteral("/home/user/projects/example"));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QPixmap shot = dlg.grab();
        QVERIFY(!shot.isNull());
        const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            QVERIFY(shot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("createrepodlg.png"))));
        }
    }
};

QTEST_MAIN(CreateRepositoryTest)

#include "createrepositorytest.moc"
