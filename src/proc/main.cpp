// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * kortoisegitproc's entry point. Plays the role of TortoiseGit src/TortoiseProc/TortoiseProc.cpp
 * (CTortoiseProcApp::InitInstance): parse the command line, create the command and run it.
 */

#include "apputils.hpp"
#include "cmdlineargs.hpp"
#include "commands/command.hpp"
#include "kortoisegit_version.hpp"
#include "kortoisegitproc_debug.hpp"
#include "utils/result.hpp"
#include "utils/runtimeerror.hpp"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>

#include <cstdio>
#include <memory>

namespace
{

using namespace kgt;

constexpr int exitFailure = 1;

/**
 * Prints an error for the user who started kortoisegitproc from a terminal.
 * @param message Translated text.
 */
void printError(const QString& message)
{
    std::fputs(qPrintable(QStringLiteral("kortoisegitproc: %1\n").arg(message)), stderr);
}

/**
 * Creates the command named on the command line and gives it its paths, switches and values.
 * @param args The parsed command line.
 * @returns The command, ready to execute, or a CommandLine error.
 */
Result<std::unique_ptr<Command>> createCommand(const CmdLineArgs& args)
{
    return CommandServer::command(args.command).map([&args](std::unique_ptr<Command> command) {
        QStringList paths = command->mayTakePath() ? args.paths : QStringList();
        // TortoiseGit accepts the current directory instead of an explicit path
        if (paths.isEmpty() && command->pathRequirement() >= PathRequirement::PathRequired) {
            paths.append(QDir::currentPath());
        }
        command->setPaths(paths);
        command->setSwitches(args.switches);
        command->setValues(args.values);
        return command;
    });
}

/**
 * Runs the command named on the command line.
 * @param parser The processed command line.
 * @returns The process exit code.
 */
int run(const QCommandLineParser& parser)
{
    const Result<std::unique_ptr<Command>> command = parseCmdLine(parser).and_then(createCommand);
    if (!command) {
        printError(command.error().message);
        return exitFailure;
    }
    try {
        return (*command)->execute() ? 0 : exitFailure;
    } catch (const RuntimeError& e) {
        qCCritical(KGT_PROC) << "Command" << parser.value(QStringLiteral("command")) << "failed:" << e.error().message;
        printError(e.error().message);
        return exitFailure;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    const QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);

    KAboutData aboutData(QStringLiteral("kortoisegitproc"),
                         i18n("KortoiseGit"),
                         QStringLiteral(KORTOISEGIT_VERSION_STRING),
                         i18n("A port of TortoiseGit to KDE. Not affiliated with the TortoiseGit project."),
                         KAboutLicense::GPL_V2,
                         i18n("© 2026 James Jones"));
    aboutData.setLicense(KAboutLicense::GPL_V2, KAboutLicense::OrLaterVersions);
    KAboutData::setApplicationData(aboutData);

    QApplication::setWindowIcon(AppUtils::appIcon());

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    addCmdLineOptions(parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    return run(parser);
}
