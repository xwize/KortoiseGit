// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/Git/Git.h (CGit): runs command-line git in one working tree. This is the git
 * layer's entry point, shared by the Dolphin plugin and kortoisegitproc.
 */

#pragma once

#include "git/gitcall.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

namespace kgt
{

/**
 * Runs git in one directory, either blocking (for the plugin, which Dolphin calls on a worker thread) or
 * asynchronously (for the app, whose UI thread must never wait on git).
 */
class Git
{
public:
    /**
     * @param workingDirectory The directory git runs in: a working tree or any directory inside one.
     */
    explicit Git(QString workingDirectory);

    /**
     * @returns The directory git runs in.
     */
    [[nodiscard]] const QString& workingDirectory() const;

    /**
     * Runs git and waits for it to exit.
     * @param arguments The arguments after "git".
     * @param mode Whether git may take the repository's optional locks.
     * @param input What git reads on stdin; empty for none.
     * @returns Everything git wrote to stdout.
     * @throws RuntimeError (category Git) if git fails to start or exits non-zero.
     */
    [[nodiscard]] QByteArray run(const QStringList& arguments, RunMode mode = RunMode::ReadWrite, const QByteArray& input = {}) const;

    /**
     * Starts git without waiting for it.
     * @param arguments The arguments after "git".
     * @param mode Whether git may take the repository's optional locks.
     * @param input What git reads on stdin; empty for none.
     * @returns The running call: connect to its finished() signal, then read its output().
     */
    [[nodiscard]] std::unique_ptr<GitCall> runAsync(const QStringList& arguments, RunMode mode = RunMode::ReadWrite, const QByteArray& input = {}) const;

    /**
     * Finds the top of the working tree holding a path, with git rev-parse --show-toplevel.
     * @param path A file or directory inside a working tree.
     * @returns The working tree's root directory, as git reports it (symbolic links resolved).
     * @throws RuntimeError (category Git) if the path is not inside a working tree.
     */
    [[nodiscard]] static QString workingTreeRoot(const QString& path);

    /**
     * Ported from CGit::StripRefName.
     * @param ref A ref such as refs/heads/main or refs/tags/v1.
     * @returns Its short name, such as main or tags/v1.
     */
    [[nodiscard]] static QString stripRefName(const QString& ref);

private:
    QString m_workingDirectory;
};

} // namespace kgt
