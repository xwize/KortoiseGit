// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/massivegittask.hpp"
#include "git/gitcall.hpp"
#include "testrepo.hpp"
#include "utils/runtimeerror.hpp"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <source_location>
#include <tuple>

namespace
{

using namespace kgt;

/**
 * @param repo A repository.
 * @returns The paths in its index, sorted.
 */
QStringList indexedPaths(const TestRepo& repo)
{
    QStringList paths;
    const QList<QByteArray> lines = repo.git().run({QStringLiteral("ls-files"), QStringLiteral("-z")}).split('\0');
    for (const QByteArray& line : lines) {
        if (!line.isEmpty()) {
            paths.append(QString::fromUtf8(line));
        }
    }
    paths.sort();
    return paths;
}

/**
 * @returns A task that adds files, as TortoiseGit's Add does.
 */
MassiveGitTask addTask()
{
    return MassiveGitTask({QStringLiteral("add"), QStringLiteral("-f")});
}

/**
 * @param task The task.
 * @param git Where to run it.
 * @returns The error it threw, or a Parse error saying it succeeded.
 */
Error runError(const MassiveGitTask& task, const Git& git)
{
    try {
        task.run(git);
    } catch (const RuntimeError& e) {
        return e.error();
    }
    return {.category = ErrorCategory::Parse, .message = QStringLiteral("The task succeeded"), .location = std::source_location::current()};
}

} // namespace

/**
 * MassiveGitTask: one git command over a list of paths handed to git on stdin.
 */
class MassiveGitTaskTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void buildsTheCommand()
    {
        MassiveGitTask task = addTask();
        QVERIFY(task.isEmpty());
        task.setPaths({QStringLiteral("a.txt"), QStringLiteral("dir/b c.txt"), QStringLiteral("a.txt")});
        QVERIFY(!task.isEmpty());
        QCOMPARE(task.arguments(),
                 (QStringList{QStringLiteral("--literal-pathspecs"),
                              QStringLiteral("add"),
                              QStringLiteral("-f"),
                              QStringLiteral("--pathspec-from-file=-"),
                              QStringLiteral("--pathspec-file-nul")}));
        // Duplicates dropped, each path NUL-terminated
        QCOMPARE(task.pathSpecFile(), QByteArray("a.txt\0dir/b c.txt\0", 18));
    }

    void putsPathSpecOptionsBeforeEndOfOptions()
    {
        MassiveGitTask task({QStringLiteral("checkout"), QStringLiteral("-f"), QStringLiteral("--end-of-options"), QStringLiteral("HEAD")});
        task.setPaths({QStringLiteral("a.txt")});
        QCOMPARE(task.arguments(),
                 (QStringList{QStringLiteral("--literal-pathspecs"),
                              QStringLiteral("checkout"),
                              QStringLiteral("-f"),
                              QStringLiteral("--pathspec-from-file=-"),
                              QStringLiteral("--pathspec-file-nul"),
                              QStringLiteral("--end-of-options"),
                              QStringLiteral("HEAD")}));
    }

    void addsPathsLiterally()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("*.txt"), "star\n");
        repo.write(QStringLiteral("other.txt"), "other\n");
        repo.write(QStringLiteral("line\nbreak.txt"), "newline\n");
        repo.write(QStringLiteral("ignored.log"), "log\n");
        repo.write(QStringLiteral(".gitignore"), "*.log\n");

        MassiveGitTask task = addTask();
        task.setPaths({QStringLiteral("*.txt"), QStringLiteral("line\nbreak.txt"), QStringLiteral("ignored.log")});
        task.run(repo.git());
        // "*.txt" is a file name, not a pattern, and -f adds the ignored file
        QCOMPARE(indexedPaths(repo), (QStringList{QStringLiteral("*.txt"), QStringLiteral("ignored.log"), QStringLiteral("line\nbreak.txt")}));
    }

    void addsManyPaths()
    {
        const TestRepo repo;
        QStringList paths;
        // Far more than one command line would hold
        for (int i = 0; i < 3000; ++i) {
            paths.append(QStringLiteral("a-rather-long-directory-name/file-with-a-long-name-%1.txt").arg(i));
        }
        repo.write(paths.constFirst(), "x\n");
        QVERIFY(std::ranges::all_of(paths, [&repo](const QString& path) {
            QFile file(repo.filePath(path));
            return file.open(QIODevice::WriteOnly);
        }));
        MassiveGitTask task = addTask();
        task.setPaths(paths);
        QVERIFY(task.pathSpecFile().size() > qsizetype{128} * 1024);
        task.run(repo.git());
        QCOMPARE(indexedPaths(repo).size(), paths.size());
    }

    void runsAsynchronously()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        MassiveGitTask task = addTask();
        task.setPaths({QStringLiteral("a.txt")});
        const std::unique_ptr<GitCall> call = task.runAsync(repo.git());
        QSignalSpy finished(call.get(), &GitCall::finished);
        QVERIFY(finished.wait(10000));
        std::ignore = call->output();
        QCOMPARE(indexedPaths(repo), QStringList{QStringLiteral("a.txt")});
    }

    void doesNothingWithoutPaths()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("a.txt"), "a\n");
        addTask().run(repo.git());
        QVERIFY(indexedPaths(repo).isEmpty());
    }

    void reportsGitsError()
    {
        const TestRepo repo;
        MassiveGitTask task = addTask();
        task.setPaths({QStringLiteral("does-not-exist.txt")});
        const Error error = runError(task, repo.git());
        QCOMPARE(error.category, ErrorCategory::Git);
        QVERIFY2(error.message.contains(QStringLiteral("does-not-exist.txt")), qPrintable(error.message));
    }
};

QTEST_GUILESS_MAIN(MassiveGitTaskTest)

#include "massivegittasktest.moc"
