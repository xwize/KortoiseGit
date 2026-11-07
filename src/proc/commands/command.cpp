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
#include "commands/commitcommand.hpp"
#include "commands/createrepositorycommand.hpp"
#include "commands/logcommand.hpp"
#include "commands/pushcommand.hpp"
#include "commands/revertcommand.hpp"

#include <KLocalizedString>

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
    CommandEntry{.name = u"commit", .create = &create<kgt::CommitCommand>},
    CommandEntry{.name = u"log", .create = &create<kgt::LogCommand>},
    CommandEntry{.name = u"push", .create = &create<kgt::PushCommand>},
    CommandEntry{.name = u"repocreate", .create = &create<kgt::CreateRepositoryCommand>},
    CommandEntry{.name = u"revert", .create = &create<kgt::RevertCommand>},
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
