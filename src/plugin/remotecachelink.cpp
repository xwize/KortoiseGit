// SPDX-FileCopyrightText: 2003-2014, 2017 TortoiseSVN
// SPDX-FileCopyrightText: 2009-2017, 2019, 2023, 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseShell/RemoteCacheLink.cpp

#include "remotecachelink.hpp"

#include "kortoisegitplugin_debug.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QProcess>

namespace
{

constexpr int callTimeoutMs = 250; // Dolphin's own retrieval takes over after this
constexpr qint64 retryAfterTimeoutMs = 10000; // m_lastTimeout's pause: don't try over and over
constexpr qint64 retryAfterStartMs = 1000; // GetStatusFromRemoteCache's wait for a cache just started

/**
 * @returns A monotonic clock, in milliseconds.
 */
qint64 monotonicMs()
{
    return QDeadlineTimer::current().deadline();
}

} // namespace

namespace kgt
{

RemoteCacheLink::RemoteCacheLink(const Options& options, QObject *parent)
    : QObject(parent)
    , m_executable(options.executable)
    , m_service(options.service)
{
    // QDBusConnection::connect only takes a slot's name. Naming the service follows whichever process owns it, and
    // starts nothing
    QDBusConnection::sessionBus()
        .connect(m_service, QString(cacheObjectPath), QString(cacheInterfaceName), QStringLiteral("itemsChanged"), this, SIGNAL(itemsChanged(QStringList)));
}

std::optional<CacheFolderStatus> RemoteCacheLink::folderStatus(const QString& directory)
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || monotonicMs() < m_retryAt) {
        return std::nullopt;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, QString(cacheObjectPath), QString(cacheInterfaceName), QStringLiteral("folderStatus"));
    call << directory;
    const QDBusMessage reply = bus.call(call, QDBus::Block, callTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        const QDBusError error(reply);
        if (error.type() == QDBusError::NoReply || error.type() == QDBusError::Timeout || error.type() == QDBusError::TimedOut) {
            // The cache didn't respond: leave it alone for a while
            m_retryAt = monotonicMs() + retryAfterTimeoutMs;
        } else {
            // Nobody owns the name and D-Bus can't start it: from the build tree there is no service file
            runCacheProcess();
        }
        return std::nullopt;
    }
    const QVariantList arguments = reply.arguments();
    if (arguments.size() != 5 || !arguments.at(0).toBool()) {
        return std::nullopt;
    }
    CacheFolderStatus status;
    if (const int wholeFolder = arguments.at(1).toInt(); wholeFolder >= 0) {
        status.wholeFolder = static_cast<GitWcStatus>(wholeFolder);
    }
    status.hasUnversioned = arguments.at(2).toBool();
    status.names = arguments.at(3).toStringList();
    status.states = qdbus_cast<QList<int>>(arguments.at(4));
    if (status.names.size() != status.states.size()) {
        return std::nullopt;
    }
    return status;
}

void RemoteCacheLink::runCacheProcess()
{
    const qint64 now = monotonicMs();
    m_retryAt = now + retryAfterStartMs;
    if (now < m_nextStartAt || m_executable.isEmpty()) {
        return;
    }
    m_nextStartAt = now + retryAfterTimeoutMs;
    QProcess process;
    process.setProgram(m_executable);
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    if (!process.startDetached()) {
        // "It's not appropriate to do a message box here, because there may be hundreds of calls"
        qCDebug(KGT_PLUGIN) << "Failed to start" << m_executable;
    }
}

} // namespace kgt
