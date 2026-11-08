// SPDX-FileCopyrightText: 2005-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2011, 2013, 2015-2017, 2019, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TGitCache/ShellUpdater.h.
 */

#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace kgt
{

/**
 * Collects the items whose status changed and reports them together, shortly after the first one. Where CShellUpdater
 * calls SHChangeNotify for each item on its own thread, one signal carries them all to every Dolphin process.
 */
class ShellUpdater : public QObject
{
    Q_OBJECT

public:
    /**
     * @param parent Owner.
     */
    explicit ShellUpdater(QObject *parent = nullptr);

    /**
     * Ported from CShellUpdater::AddPathForUpdate.
     * @param path An item whose status changed.
     */
    void addPathForUpdate(const QString& path);

Q_SIGNALS:
    /**
     * @param paths The items that changed since the last signal, each once.
     */
    void pathsChanged(const QStringList& paths);

private:
    QSet<QString> m_pathsToUpdate;
    QTimer m_wakeTimer; // CShellUpdater::WorkerThread's "wait some time before we notify the shell"
};

} // namespace kgt
