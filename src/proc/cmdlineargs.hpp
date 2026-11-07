// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * kortoisegitproc's command line, TortoiseGitProc's /command: and /path: in standard Unix form:
 * --command <name> and a repeatable --path <path>. Plays the role of TortoiseGit's CCmdLineParser
 * (src/Utils/CmdLineParser.h) on top of QCommandLineParser.
 */

#pragma once

#include "utils/result.hpp"

#include <QString>
#include <QStringList>

class QCommandLineParser;

namespace kgt
{

/**
 * The arguments that choose and drive a command.
 */
struct CmdLineArgs {
    QString command; // TortoiseGit's command name, such as "log"
    QStringList paths; // Absolute paths, in the order given; empty if no --path was given
};

/**
 * Adds --command and --path to @p parser. --help and --version come from KAboutData::setupCommandLine().
 * @param[in,out] parser The parser to add the options to.
 */
void addCmdLineOptions(QCommandLineParser& parser);

/**
 * Reads the options added by addCmdLineOptions() from a parser that has already parsed the command line.
 * Relative paths are resolved against the current directory.
 * @param parser The parser, after QCommandLineParser::process() or parse().
 * @returns The command and its paths, or a CommandLine error if --command is missing or empty.
 */
[[nodiscard]] Result<CmdLineArgs> parseCmdLine(const QCommandLineParser& parser);

} // namespace kgt
