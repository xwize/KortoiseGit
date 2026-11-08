// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cmdlineargs.hpp"
#include "commands/command.hpp"
#include "commands/logcommand.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QTest>

namespace
{

using namespace kgt;

/**
 * @param arguments The arguments after the program name.
 * @returns What parseCmdLine() makes of them.
 */
Result<CmdLineArgs> parse(const QStringList& arguments)
{
    QCommandLineParser parser;
    addCmdLineOptions(parser);
    const bool parsed = parser.parse(QStringList{QStringLiteral("kortoisegitproc")} + arguments);
    Q_ASSERT(parsed);
    return parseCmdLine(parser);
}

} // namespace

/**
 * Argument parsing (cmdlineargs.hpp) and dispatch by name (CommandServer).
 */
class CmdLineArgsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesCommandAndPaths()
    {
        const Result<CmdLineArgs> args = parse({QStringLiteral("--command"),
                                                QStringLiteral("log"),
                                                QStringLiteral("--path"),
                                                QStringLiteral("/repo/a"),
                                                QStringLiteral("--path"),
                                                QStringLiteral("/repo/b")});
        QVERIFY(args);
        QCOMPARE(args->command, QStringLiteral("log"));
        QCOMPARE(args->paths, QStringList({QStringLiteral("/repo/a"), QStringLiteral("/repo/b")}));
    }

    void parsesSwitches()
    {
        Result<CmdLineArgs> args = parse({QStringLiteral("--command"), QStringLiteral("merge"), QStringLiteral("--abort")});
        QVERIFY(args);
        QCOMPARE(args->switches, QStringList{QStringLiteral("abort")});
        args = parse({QStringLiteral("--command"), QStringLiteral("merge")});
        QVERIFY(args);
        QVERIFY(args->switches.isEmpty());
        QVERIFY(args->values.isEmpty());
    }

    void parsesValues()
    {
        const Result<CmdLineArgs> args = parse({QStringLiteral("--command"), QStringLiteral("switch"), QStringLiteral("--rev"), QStringLiteral("v1")});
        QVERIFY(args);
        QCOMPARE(args->values.value(QStringLiteral("rev")), QStringLiteral("v1"));
    }

    void parsesBranchName()
    {
        const Result<CmdLineArgs> args = parse({QStringLiteral("--command"), QStringLiteral("branch"), QStringLiteral("--name"), QStringLiteral("topic")});
        QVERIFY(args);
        QCOMPARE(args->values.value(QStringLiteral("name")), QStringLiteral("topic"));
    }

    void parsesRebaseOptions()
    {
        const Result<CmdLineArgs> args = parse(
            {QStringLiteral("--command"), QStringLiteral("rebase"), QStringLiteral("--upstream"), QStringLiteral("origin/main"), QStringLiteral("--force")});
        QVERIFY(args);
        QCOMPARE(args->values.value(QStringLiteral("upstream")), QStringLiteral("origin/main"));
        QCOMPARE(args->switches, QStringList{QStringLiteral("force")});
    }

    void makesRelativePathsAbsolute()
    {
        const Result<CmdLineArgs> args = parse({QStringLiteral("--command"), QStringLiteral("log"), QStringLiteral("--path"), QStringLiteral("sub/../file")});
        QVERIFY(args);
        QCOMPARE(args->paths, QStringList{QDir::current().absoluteFilePath(QStringLiteral("file"))});
    }

    void pathsAreOptional()
    {
        const Result<CmdLineArgs> args = parse({QStringLiteral("--command"), QStringLiteral("log")});
        QVERIFY(args);
        QVERIFY(args->paths.isEmpty());
    }

    void missingCommandIsAnError()
    {
        const Result<CmdLineArgs> args = parse({QStringLiteral("--path"), QStringLiteral("/repo")});
        QVERIFY(!args);
        QCOMPARE(args.error().category, ErrorCategory::CommandLine);
        QVERIFY(!args.error().message.isEmpty());
    }

    void dispatchesLog()
    {
        const Result<std::unique_ptr<Command>> command = CommandServer::command(QStringLiteral("log"));
        QVERIFY(command);
        QVERIFY(dynamic_cast<LogCommand *>(command->get()) != nullptr);
        QCOMPARE((*command)->pathRequirement(), PathRequirement::WorkingTreeOrBareRepoRequired);
    }

    void commandKeepsSeveralPaths()
    {
        const Result<std::unique_ptr<Command>> command = CommandServer::command(QStringLiteral("log"));
        QVERIFY(command);
        const QStringList paths{QStringLiteral("/repo/a"), QStringLiteral("/repo/b"), QStringLiteral("/repo/c")};
        (*command)->setPaths(paths);
        QCOMPARE((*command)->paths(), paths);
    }

    void unknownCommandIsAnError()
    {
        const Result<std::unique_ptr<Command>> command = CommandServer::command(QStringLiteral("nosuchcommand"));
        QVERIFY(!command);
        QCOMPARE(command.error().category, ErrorCategory::CommandLine);
        QVERIFY(command.error().message.contains(QStringLiteral("nosuchcommand")));
    }

    void readsPathsFromAFile()
    {
        QTemporaryFile file;
        file.setAutoRemove(false);
        QVERIFY(file.open());
        file.write(QByteArray("/repo/a\0/repo/new\nline\0", 24));
        file.close();
        const Result<CmdLineArgs> args =
            parse({QStringLiteral("--command"), QStringLiteral("add"), QStringLiteral("--pathfile"), file.fileName(), QStringLiteral("--deletepathfile")});
        QVERIFY(args);
        QCOMPARE(args->paths, (QStringList{QStringLiteral("/repo/a"), QStringLiteral("/repo/new\nline")}));
        QVERIFY(!QFile::exists(file.fileName()));
    }

    void keepsAPathFileUnlessAskedToDeleteIt()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write(QByteArray("/repo/a\0", 8));
        file.close();
        QVERIFY(parse({QStringLiteral("--command"), QStringLiteral("add"), QStringLiteral("--pathfile"), file.fileName()}));
        QVERIFY(QFile::exists(file.fileName()));
    }

    void pathAndPathFileExcludeEachOther()
    {
        QVERIFY(!parse({QStringLiteral("--command"),
                        QStringLiteral("add"),
                        QStringLiteral("--path"),
                        QStringLiteral("/a"),
                        QStringLiteral("--pathfile"),
                        QStringLiteral("/b")}));
    }

    void anUnreadablePathFileIsAnError()
    {
        QVERIFY(!parse({QStringLiteral("--command"), QStringLiteral("add"), QStringLiteral("--pathfile"), QStringLiteral("/nonexistent/paths")}));
    }

    void parsesLogRange()
    {
        const Result<CmdLineArgs> args =
            parse({QStringLiteral("--command"), QStringLiteral("log"), QStringLiteral("--range"), QStringLiteral("refs/heads/topic")});
        QVERIFY(args);
        QCOMPARE(args->values.value(QStringLiteral("range")), QStringLiteral("refs/heads/topic"));
    }
};

QTEST_GUILESS_MAIN(CmdLineArgsTest)

#include "cmdlineargstest.moc"
