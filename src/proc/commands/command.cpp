// SPDX-FileCopyrightText: 2007-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/Commands/Command.cpp.
 */

#include "commands/command.hpp"

#include "commands/addcommand.hpp"
#include "commands/branchcommand.hpp"
#include "commands/clonecommand.hpp"
#include "commands/commitcommand.hpp"
#include "commands/createrepositorycommand.hpp"
#include "commands/fetchcommand.hpp"
#include "commands/logcommand.hpp"
#include "commands/mergecommand.hpp"
#include "commands/pullcommand.hpp"
#include "commands/pushcommand.hpp"
#include "commands/rebasecommand.hpp"
#include "commands/refbrowsecommand.hpp"
#include "commands/revertcommand.hpp"
#include "commands/switchcommand.hpp"

#include "git/git.hpp"
#include "utils/runtimeerror.hpp"

#include <KLocalizedString>

#include <QFileInfo>

#include <array>
#include <utility>

namespace
{

/**
 * One command name and how to create its command, TortoiseGit's commandInfo table.
 */
struct CommandEntry {
    QStringView name; // TortoiseGit's command name
    std::unique_ptr<kgt::Command> (*create)() = nullptr; // Creates the command
};

template<typename T>
std::unique_ptr<kgt::Command> create()
{
    return std::make_unique<T>();
}

constexpr std::array commands{
    CommandEntry{.name = u"add", .create = &create<kgt::AddCommand>},
    CommandEntry{.name = u"branch", .create = &create<kgt::BranchCommand>},
    CommandEntry{.name = u"clone", .create = &create<kgt::CloneCommand>},
    CommandEntry{.name = u"commit", .create = &create<kgt::CommitCommand>},
    CommandEntry{.name = u"fetch", .create = &create<kgt::FetchCommand>},
    CommandEntry{.name = u"log", .create = &create<kgt::LogCommand>},
    CommandEntry{.name = u"merge", .create = &create<kgt::MergeCommand>},
    CommandEntry{.name = u"pull", .create = &create<kgt::PullCommand>},
    CommandEntry{.name = u"push", .create = &create<kgt::PushCommand>},
    CommandEntry{.name = u"rebase", .create = &create<kgt::RebaseCommand>},
    CommandEntry{.name = u"refbrowse", .create = &create<kgt::RefBrowseCommand>},
    CommandEntry{.name = u"repocreate", .create = &create<kgt::CreateRepositoryCommand>},
    CommandEntry{.name = u"revert", .create = &create<kgt::RevertCommand>},
    CommandEntry{.name = u"switch", .create = &create<kgt::SwitchCommand>},
};

} // namespace

namespace kgt
{

Command::Command(PathRequirement requirement)
    : m_pathRequirement(requirement)
{
}

PathRequirement Command::pathRequirement() const
{
    return m_pathRequirement;
}

bool Command::mayTakePath() const
{
    return m_pathRequirement >= PathRequirement::MayTakePath;
}

void Command::setPaths(const QStringList& paths)
{
    m_paths = paths;
}

const QStringList& Command::paths() const
{
    return m_paths;
}

void Command::setSwitches(const QStringList& switches)
{
    m_switches = switches;
}

bool Command::hasKey(const QString& name) const
{
    return m_switches.contains(name);
}

void Command::setValues(const QMap<QString, QString>& values)
{
    m_values = values;
}

QString Command::value(const QString& name) const
{
    return m_values.value(name);
}

QString Command::repositoryRoot(const QString& path)
{
    try {
        return Git::workingTreeRoot(path);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        const QFileInfo info(path);
        return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }
}

Result<std::unique_ptr<Command>> CommandServer::command(const QString& name)
{
    for (const CommandEntry& entry : commands) {
        if (entry.name == name) {
            return entry.create();
        }
    }
    return makeError(ErrorCategory::CommandLine, i18n("Unknown command \"%1\". Run kortoisegitproc --help for usage.", name));
}

} // namespace kgt
