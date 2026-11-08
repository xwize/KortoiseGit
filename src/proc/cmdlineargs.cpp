// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cmdlineargs.hpp"

#include <KLocalizedString>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>

namespace
{

using namespace Qt::StringLiterals;

constexpr auto commandOption = "command"_L1;
constexpr auto pathOption = "path"_L1;
// TortoiseGit's /abort: MergeCommand aborts a merge instead of starting one
constexpr auto abortSwitch = "abort"_L1;
// TortoiseGit's /rev: SwitchCommand preselects this ref
constexpr auto revOption = "rev"_L1;
// TortoiseGit's /force: RebaseCommand starts with "Force Rebase" checked
constexpr auto forceSwitch = "force"_L1;
// TortoiseGit's /upstream: RebaseCommand preselects this upstream
constexpr auto upstreamOption = "upstream"_L1;

} // namespace

namespace kgt
{

void addCmdLineOptions(QCommandLineParser& parser)
{
    parser.addOption(QCommandLineOption(commandOption, i18n("The command to run, such as log."), i18nc("@info:shell", "name")));
    parser.addOption(
        QCommandLineOption(pathOption, i18n("A file or folder to run the command on. Can be given more than once."), i18nc("@info:shell", "path")));
    parser.addOption(QCommandLineOption(abortSwitch, i18n("With --command merge: abort the merge in progress.")));
    parser.addOption(QCommandLineOption(revOption, i18n("With --command switch: the branch, tag or commit to preselect."), i18nc("@info:shell", "ref")));
    parser.addOption(QCommandLineOption(forceSwitch, i18n("With --command rebase: check \"Force Rebase\".")));
    parser.addOption(QCommandLineOption(upstreamOption, i18n("With --command rebase: the upstream to preselect."), i18nc("@info:shell", "ref")));
}

Result<CmdLineArgs> parseCmdLine(const QCommandLineParser& parser)
{
    const QString command = parser.value(commandOption);
    if (command.isEmpty()) {
        return makeError(ErrorCategory::CommandLine, i18n("No command given. Use --command <name>, for example --command log."));
    }

    QStringList paths;
    const QDir current = QDir::current();
    for (const QString& path : parser.values(pathOption)) {
        paths.append(QDir::cleanPath(current.absoluteFilePath(path)));
    }
    QStringList switches;
    for (const QLatin1StringView name : {abortSwitch, forceSwitch}) {
        if (parser.isSet(name)) {
            switches.append(name);
        }
    }
    QMap<QString, QString> values;
    for (const QLatin1StringView name : {revOption, upstreamOption}) {
        if (parser.isSet(name)) {
            values.insert(name, parser.value(name));
        }
    }
    return CmdLineArgs{.command = command, .paths = paths, .switches = switches, .values = values};
}

} // namespace kgt
