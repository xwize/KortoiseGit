// SPDX-FileCopyrightText: 2005-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2011, 2013, 2015-2017, 2019, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TGitCache/ShellUpdater.cpp

#include "shellupdater.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace
{

constexpr std::chrono::milliseconds notifyDelay{50}; // CShellUpdater::WorkerThread's Sleep(50)

} // namespace

namespace kgt
{

ShellUpdater::ShellUpdater(QObject *parent)
    : QObject(parent)
{
    m_wakeTimer.setSingleShot(true);
    m_wakeTimer.setInterval(notifyDelay);
    connect(&m_wakeTimer, &QTimer::timeout, this, [this] {
        QStringList paths(m_pathsToUpdate.cbegin(), m_pathsToUpdate.cend());
        m_pathsToUpdate.clear();
        std::ranges::sort(paths);
        Q_EMIT pathsChanged(paths);
    });
}

void ShellUpdater::addPathForUpdate(const QString& path)
{
    m_pathsToUpdate.insert(path);
    if (!m_wakeTimer.isActive()) {
        m_wakeTimer.start();
    }
}

} // namespace kgt
