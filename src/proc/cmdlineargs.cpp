// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "cmdlineargs.hpp"

#include <KLocalizedString>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>

#include <utility>

namespace
{

using namespace Qt::StringLiterals;

constexpr auto commandOption = "command"_L1;
constexpr auto pathOption = "path"_L1;
// TortoiseGit's /pathfile and /deletepathfile: the plugin's way to pass a selection too long for a command line
constexpr auto pathFileOption = "pathfile"_L1;
constexpr auto deletePathFileSwitch = "deletepathfile"_L1;
// TortoiseGit's /range: LogCommand lists these revisions instead of HEAD's history
constexpr auto rangeOption = "range"_L1;
// TortoiseGit's /abort: MergeCommand aborts a merge instead of starting one
constexpr auto abortSwitch = "abort"_L1;
// TortoiseGit's /rev: SwitchCommand, BranchCommand and TagCommand preselect this ref
constexpr auto revOption = "rev"_L1;
// TortoiseGit's /name: BranchCommand and TagCommand fill in this name
constexpr auto nameOption = "name"_L1;
// TortoiseGit's /force: RebaseCommand starts with "Force Rebase" checked
constexpr auto forceSwitch = "force"_L1;
// TortoiseGit's /upstream: RebaseCommand preselects this upstream
constexpr auto upstreamOption = "upstream"_L1;
// TortoiseGit's /path2: DiffCommand compares --path with this file
constexpr auto path2Option = "path2"_L1;
// TortoiseGit's /startrev and /endrev: DiffCommand compares these two revisions
constexpr auto startRevOption = "startrev"_L1;
constexpr auto endRevOption = "endrev"_L1;

/**
 * Ported from the /pathfile handling in CTortoiseProcApp::InitInstance.
 * @param file A file holding NUL-terminated paths.
 * @param deleteFile Whether to delete it afterwards.
 * @returns The paths, or a CommandLine error if it can't be read.
 */
kgt::Result<QStringList> readPathFile(const QString& file, bool deleteFile)
{
    QFile pathFile(file);
    if (!pathFile.open(QIODevice::ReadOnly)) {
        return kgt::makeError(kgt::ErrorCategory::CommandLine, i18n("Could not read the path file \"%1\".", file));
    }
    QStringList paths;
    for (const QByteArray& path : pathFile.readAll().split('\0')) {
        if (!path.isEmpty()) {
            paths.append(QString::fromUtf8(path));
        }
    }
    if (deleteFile) {
        pathFile.remove();
    }
    return paths;
}

} // namespace

namespace kgt
{

void addCmdLineOptions(QCommandLineParser& parser)
{
    parser.addOption(QCommandLineOption(commandOption, i18n("The command to run, such as log."), i18nc("@info:shell", "name")));
    parser.addOption(
        QCommandLineOption(pathOption, i18n("A file or folder to run the command on. Can be given more than once."), i18nc("@info:shell", "path")));
    parser.addOption(
        QCommandLineOption(pathFileOption, i18n("A file listing the paths to run the command on, each ended by a NUL."), i18nc("@info:shell", "file")));
    parser.addOption(QCommandLineOption(deletePathFileSwitch, i18n("Delete the --pathfile file after reading it.")));
    parser.addOption(QCommandLineOption(rangeOption, i18n("With --command log: the revisions to list."), i18nc("@info:shell", "range")));
    parser.addOption(QCommandLineOption(abortSwitch, i18n("With --command merge: abort the merge in progress.")));
    parser.addOption(
        QCommandLineOption(revOption, i18n("With --command switch, branch or tag: the branch, tag or commit to preselect."), i18nc("@info:shell", "ref")));
    parser.addOption(QCommandLineOption(nameOption, i18n("With --command branch or tag: the new branch's or tag's name."), i18nc("@info:shell", "name")));
    parser.addOption(QCommandLineOption(forceSwitch, i18n("With --command rebase: check \"Force Rebase\".")));
    parser.addOption(QCommandLineOption(upstreamOption, i18n("With --command rebase: the upstream to preselect."), i18nc("@info:shell", "ref")));
    parser.addOption(QCommandLineOption(path2Option, i18n("With --command diff: a second file to compare with --path."), i18nc("@info:shell", "path")));
    parser.addOption(QCommandLineOption(startRevOption, i18n("With --command diff: the revision to compare from."), i18nc("@info:shell", "ref")));
    parser.addOption(QCommandLineOption(endRevOption, i18n("With --command diff: the revision to compare to."), i18nc("@info:shell", "ref")));
}

Result<CmdLineArgs> parseCmdLine(const QCommandLineParser& parser)
{
    const QString command = parser.value(commandOption);
    if (command.isEmpty()) {
        return makeError(ErrorCategory::CommandLine, i18n("No command given. Use --command <name>, for example --command log."));
    }

    if (parser.isSet(pathOption) && parser.isSet(pathFileOption)) {
        return makeError(ErrorCategory::CommandLine, i18n("Use --path or --pathfile, not both."));
    }
    QStringList givenPaths = parser.values(pathOption);
    if (parser.isSet(pathFileOption)) {
        Result<QStringList> listed = readPathFile(parser.value(pathFileOption), parser.isSet(deletePathFileSwitch));
        if (!listed) {
            return tl::unexpected(std::move(listed).error());
        }
        givenPaths = std::move(*listed);
    }

    QStringList paths;
    const QDir current = QDir::current();
    for (const QString& path : std::as_const(givenPaths)) {
        paths.append(QDir::cleanPath(current.absoluteFilePath(path)));
    }
    QStringList switches;
    for (const QLatin1StringView name : {abortSwitch, forceSwitch}) {
        if (parser.isSet(name)) {
            switches.append(name);
        }
    }
    QMap<QString, QString> values;
    for (const QLatin1StringView name : {revOption, nameOption, upstreamOption, startRevOption, endRevOption, rangeOption}) {
        if (parser.isSet(name)) {
            values.insert(name, parser.value(name));
        }
    }
    if (parser.isSet(path2Option)) {
        values.insert(path2Option, QDir::cleanPath(current.absoluteFilePath(parser.value(path2Option))));
    }
    return CmdLineArgs{.command = command, .paths = paths, .switches = switches, .values = values};
}

} // namespace kgt
