// SPDX-FileCopyrightText: 2009-2014, 2016, 2019, 2022-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/ProgressCommands/AddProgressCommand.cpp

#include "progresscommands/addprogresscommand.hpp"

#include "git/git.hpp"
#include "git/gitcall.hpp"
#include "git/massivegittask.hpp"
#include "progressdlg.hpp"
#include "utils/runtimeerror.hpp"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace kgt
{

AddProgressCommand::AddProgressCommand(FileMode fileMode, QObject *parent)
    : QObject(parent)
    , m_fileMode(fileMode)
{
}

AddProgressCommand::~AddProgressCommand() = default;

void AddProgressCommand::setPathList(const QStringList& paths)
{
    m_paths = paths;
}

const QStringList& AddProgressCommand::pathList() const
{
    return m_paths;
}

void AddProgressCommand::run(const QString& workingTree, QWidget *dialogParent)
{
    m_workingTree = workingTree;
    m_dialogParent = dialogParent;
    m_error.reset();
    m_fileModePaths.clear();
    if (m_fileMode != FileMode::Unchanged) {
        // SetFileMode skips folders. A symlink on disk is added as one already, and git can't make it executable
        const QDir root(workingTree);
        for (const QString& path : std::as_const(m_paths)) {
            const QFileInfo file(root.filePath(path));
            if (!file.isDir() && !file.isSymLink()) {
                m_fileModePaths.append(path);
            }
        }
    }
    if (m_fileMode != FileMode::Symlink || m_fileModePaths.isEmpty()) {
        showProgress({});
        return;
    }
    // git update-index needs each blob's hash to record a new mode; git add will store the same blobs
    m_hashCall = Git(workingTree).runAsync(QStringList{QStringLiteral("hash-object"), QStringLiteral("-w"), QStringLiteral("--")} + m_fileModePaths);
    connect(m_hashCall.get(), &GitCall::finished, this, &AddProgressCommand::onHashesFinished);
}

const std::optional<Error>& AddProgressCommand::error() const
{
    return m_error;
}

void AddProgressCommand::onHashesFinished()
{
    // Deleted from the event loop: it is still emitting
    GitCall *call = m_hashCall.release();
    call->deleteLater();
    QByteArray output;
    try {
        output = call->output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        m_error = e.error();
        Q_EMIT finished();
        return;
    }
    const QList<QByteArray> hashes = output.split('\n');
    Q_ASSERT(hashes.size() > m_fileModePaths.size());
    QByteArray indexInfo;
    for (qsizetype i = 0; i < m_fileModePaths.size(); ++i) {
        indexInfo += "120000 " + hashes.at(i) + '\t' + m_fileModePaths.at(i).toUtf8() + '\0';
    }
    showProgress(indexInfo);
}

void AddProgressCommand::showProgress(const QByteArray& symlinkIndexInfo)
{
    auto *dialog = new ProgressDlg(m_workingTree, m_dialogParent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    MassiveGitTask addTask({QStringLiteral("add"), QStringLiteral("-f")});
    addTask.setPaths(m_paths);
    dialog->addGitCmd(addTask.arguments(), addTask.pathSpecFile());
    if (m_fileMode == FileMode::Executable && !m_fileModePaths.isEmpty()) {
        MassiveGitTask chmodTask({QStringLiteral("add"), QStringLiteral("-f"), QStringLiteral("--chmod=+x")});
        chmodTask.setPaths(m_fileModePaths);
        dialog->addGitCmd(chmodTask.arguments(), chmodTask.pathSpecFile());
    } else if (!symlinkIndexInfo.isEmpty()) {
        dialog->addGitCmd({QStringLiteral("update-index"), QStringLiteral("-z"), QStringLiteral("--index-info")}, symlinkIndexInfo);
    }
    connect(dialog, &QDialog::finished, this, &AddProgressCommand::finished);
    dialog->open();
}

} // namespace kgt
