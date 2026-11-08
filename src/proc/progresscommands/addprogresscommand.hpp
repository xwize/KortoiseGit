// SPDX-FileCopyrightText: 2009-2014, 2016, 2019, 2022-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ProgressCommands/AddProgressCommand.h: adds files to the index in a
 * progress dialog, optionally recording them as executables or symlinks. TortoiseGit's CGitProgressDlg is not
 * ported, so this runs in ProgressDlg; its post-command buttons aren't either.
 */

#pragma once

#include "utils/error.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class QWidget;

namespace kgt
{

class GitCall;

/**
 * Runs git add -f on some paths in a window-modal ProgressDlg, then sets the index mode of the files among them if
 * asked (AddProgressCommand::Run and SetFileMode).
 */
class AddProgressCommand : public QObject
{
    Q_OBJECT

public:
    /**
     * What the files are recorded as in the index, besides being added.
     */
    enum class FileMode {
        Unchanged, // As they are on disk
        Executable, // GIT_FILEMODE_BLOB_EXECUTABLE (SetExecutable)
        Symlink, // GIT_FILEMODE_LINK (SetSymlink)
    };

    /**
     * @param fileMode What to record the files as.
     * @param parent The owning object.
     */
    explicit AddProgressCommand(FileMode fileMode = FileMode::Unchanged, QObject *parent = nullptr);
    ~AddProgressCommand() override;

    AddProgressCommand(const AddProgressCommand&) = delete;
    AddProgressCommand& operator=(const AddProgressCommand&) = delete;
    AddProgressCommand(AddProgressCommand&&) = delete;
    AddProgressCommand& operator=(AddProgressCommand&&) = delete;

    /**
     * @param paths The paths to add, relative to the working tree root (m_targetPathList).
     */
    void setPathList(const QStringList& paths);

    /**
     * @returns The paths to add.
     */
    [[nodiscard]] const QStringList& pathList() const;

    /**
     * Opens the progress dialog and runs the commands without blocking (AddProgressCommand::Run). finished() follows
     * when the dialog is closed.
     * @param workingTree The top of the working tree.
     * @param dialogParent The window the dialog blocks; it must outlive this command.
     */
    void run(const QString& workingTree, QWidget *dialogParent);

    /**
     * @returns Why the dialog could not be opened, or nothing. What git itself reports is shown in the dialog.
     */
    [[nodiscard]] const std::optional<Error>& error() const;

Q_SIGNALS:
    /**
     * The dialog was closed, or could not be opened.
     */
    void finished();

private:
    void onHashesFinished();
    void showProgress(const QByteArray& symlinkIndexInfo);

    FileMode m_fileMode;
    QStringList m_paths;
    QStringList m_fileModePaths; // The paths whose index mode is set
    QString m_workingTree;
    QWidget *m_dialogParent = nullptr;
    std::unique_ptr<GitCall> m_hashCall; // git hash-object, before a symlink's dialog
    std::optional<Error> m_error;
};

} // namespace kgt
