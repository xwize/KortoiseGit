// SPDX-FileCopyrightText: 2009-2016, 2019, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/ProgressCommands/ResolveProgressCommand.cpp

#include "progresscommands/resolveprogresscommand.hpp"

#include "adddlg.hpp"
#include "git/massivegittask.hpp"
#include "git/tgitpath.hpp"
#include "progressdlg.hpp"

#include <KLocalizedString>

#include <QDir>
#include <QStringList>

namespace
{

/**
 * @param action What happened to the files, CGitProgressList's action column.
 * @param paths The files.
 * @returns One line for each file.
 */
QString notifyLines(const QString& action, const QStringList& paths)
{
    QString lines;
    for (const QString& path : paths) {
        lines += QStringLiteral("%1  %2\n").arg(action, path);
    }
    return lines;
}

} // namespace

namespace kgt
{

void ResolveProgressCommand::setPathList(const QList<GitStatusListEntry>& entries)
{
    m_targetPathList = entries;
}

void ResolveProgressCommand::setUp(const QString& workingTree, ProgressDlg& dialog) const
{
    QStringList unmerged;
    QStringList skipped;
    QStringList absolutePaths;
    const QDir root(workingTree);
    for (const GitStatusListEntry& entry : m_targetPathList) {
        (entry.actions & TGitPath::Action::Unmerged ? unmerged : skipped).append(entry.path);
        absolutePaths.append(root.filePath(entry.path));
    }

    // CGitProgressList::SetWindowTitle: IDS_PROGRS_TITLE_RESOLVE on the paths' common root
    const QString commonRoot = QDir::toNativeSeparators(AddDlg::commonRoot(absolutePaths));
    dialog.setWindowTitle(QStringLiteral("%1 - %2 - %3").arg(commonRoot, i18n("Resolve"), i18n("KortoiseGit")));

    MassiveGitTask addTask({QStringLiteral("add"), QStringLiteral("-f")});
    addTask.setPaths(unmerged);
    if (!addTask.isEmpty()) {
        dialog.addGitCmd(addTask.arguments(), addTask.pathSpecFile());
    }

    // CGitProgressList::AddNotify's lines, in the log: ProgressDlg has no list of files
    dialog.setPostExecCallback([unmerged, skipped](int& exitCode, QString& extraMsg) {
        // Every path was skipped: ProgressDlg calls having no command a failure, CMassiveGitTask an empty success
        if (unmerged.isEmpty()) {
            exitCode = 0;
        }
        extraMsg = notifyLines(i18n("Skipped"), skipped);
        if (exitCode == 0) {
            extraMsg += notifyLines(i18n("Resolved"), unmerged);
            // ResolveProgressCommand::ShowInfo
            extraMsg += i18n("You need commit your change after resolve conflict") + QLatin1Char('\n');
        }
    });
}

} // namespace kgt
