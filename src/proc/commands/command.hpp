// SPDX-FileCopyrightText: 2007 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2009, 2011, 2013-2014, 2016, 2018-2019, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/Command.h: the interface of every kortoisegitproc command
 * and the factory that picks one by name.
 */

#pragma once

#include "utils/result.hpp"

#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>

namespace kgt
{

/**
 * What a command needs as its --path, from TortoiseGit's PathRequirement. Ordered: each value implies the ones
 * before it.
 */
enum class PathRequirement {
    NoPathRequired, // The command ignores --path
    MayTakePath, // --path is optional
    PathRequired, // A path is needed; the current directory is used when none is given, as in TortoiseGit
    WorkingTreeOrBareRepoRequired, // The path must be in a working tree or a bare repository
    WorkingTreeRequired, // The path must be in a working tree
};

/**
 * One command kortoisegitproc can run, such as log or commit. Each TortoiseGit Commands/<Name>Command class has
 * a subclass of the same name.
 */
class Command
{
public:
    virtual ~Command() = default;
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;

    /**
     * Runs the command, usually by showing its dialog until the user closes it.
     * @returns True on success, false if the command failed or was cancelled.
     */
    [[nodiscard]] virtual bool execute() = 0;

    /**
     * @returns What the command needs as its --path.
     */
    [[nodiscard]] PathRequirement pathRequirement() const;

    /**
     * @returns True if the command uses --path at all.
     */
    [[nodiscard]] bool mayTakePath() const;

    /**
     * @param paths The absolute paths from --path, in the order given.
     */
    void setPaths(const QStringList& paths);

    /**
     * @returns The paths set with setPaths().
     */
    [[nodiscard]] const QStringList& paths() const;

    /**
     * @param switches The switches given on the command line, such as "abort".
     */
    void setSwitches(const QStringList& switches);

    /**
     * Ported from CCmdLineParser::HasKey, for switches.
     * @param name A switch's name, such as "abort".
     * @returns Whether it was given.
     */
    [[nodiscard]] bool hasKey(const QString& name) const;

    /**
     * @param values The options given with a value on the command line, such as "rev".
     */
    void setValues(const QMap<QString, QString>& values);

    /**
     * Ported from CCmdLineParser::GetVal.
     * @param name An option's name, such as "rev".
     * @returns Its value, or an empty string if it wasn't given.
     */
    [[nodiscard]] QString value(const QString& name) const;

protected:
    /**
     * @param requirement What the command needs as its --path.
     */
    explicit Command(PathRequirement requirement);

    /**
     * @param path A path in a working tree or a bare repository.
     * @returns The working tree's root holding it, or, for a bare repository, which has none, the path itself.
     */
    [[nodiscard]] static QString repositoryRoot(const QString& path);

private:
    PathRequirement m_pathRequirement;
    QStringList m_paths;
    QStringList m_switches;
    QMap<QString, QString> m_values;
};

/**
 * The factory for the commands kortoisegitproc runs, from TortoiseGit's CommandServer.
 */
class CommandServer
{
public:
    CommandServer() = delete;

    /**
     * @param name TortoiseGit's command name, such as "log".
     * @returns A new command, or a CommandLine error if no command has that name.
     */
    [[nodiscard]] static Result<std::unique_ptr<Command>> command(const QString& name);
};

} // namespace kgt
