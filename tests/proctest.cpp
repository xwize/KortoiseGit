// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QProcess>
#include <QTest>

namespace
{

/**
 * The result of one run of kortoisegitproc.
 */
struct ProcRun {
    int exitCode = -1; // The process exit code
    QString output; // Everything it printed, stdout and stderr together
};

/**
 * Runs the built kortoisegitproc and waits for it to exit.
 * @param arguments Its arguments.
 * @returns How it exited and what it printed.
 */
ProcRun runProc(const QStringList& arguments)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral(KGT_PROC_EXE), arguments);
    if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit) {
        return {};
    }
    return {.exitCode = process.exitCode(), .output = QString::fromLocal8Bit(process.readAll())};
}

} // namespace

/**
 * kortoisegitproc's exit codes and messages, run as a separate process. Commands that open a dialog are left
 * to the dialog tests, since they wait for the user.
 */
class ProcTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void missingCommandFails()
    {
        const ProcRun run = runProc({QStringLiteral("--path"), QStringLiteral("/tmp")});
        QCOMPARE(run.exitCode, 1);
        QVERIFY2(run.output.contains(QStringLiteral("--command")), qPrintable(run.output));
    }

    void unknownCommandFails()
    {
        const ProcRun run = runProc({QStringLiteral("--command"), QStringLiteral("nosuchcommand")});
        QCOMPARE(run.exitCode, 1);
        QVERIFY2(run.output.contains(QStringLiteral("nosuchcommand")), qPrintable(run.output));
    }

    void missingOptionValueFails()
    {
        const ProcRun run = runProc({QStringLiteral("--command")});
        QVERIFY(run.exitCode != 0);
    }

    void helpListsOptions()
    {
        const ProcRun run = runProc({QStringLiteral("--help")});
        QCOMPARE(run.exitCode, 0);
        QVERIFY2(run.output.contains(QStringLiteral("--command")) && run.output.contains(QStringLiteral("--path")), qPrintable(run.output));
    }

    void versionWorks()
    {
        const ProcRun run = runProc({QStringLiteral("--version")});
        QCOMPARE(run.exitCode, 0);
        QVERIFY2(run.output.contains(QStringLiteral("kortoisegitproc")), qPrintable(run.output));
    }
};

QTEST_GUILESS_MAIN(ProcTest)

#include "proctest.moc"
