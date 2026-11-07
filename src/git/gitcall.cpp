// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/Git.cpp (CGit::Run, CGit::RunAsync)

#include "git/gitcall.hpp"

#include "utils/runtimeerror.hpp"

#include <QMetaObject>
#include <QProcessEnvironment>

#include <algorithm>

namespace
{

constexpr auto gitProgram = "git";

/**
 * @param argument One argument.
 * @returns It, in double quotes if it is empty or holds whitespace, for a readable command line.
 */
QString quoted(const QString& argument)
{
    const bool needsQuotes = argument.isEmpty() || std::any_of(argument.cbegin(), argument.cend(), [](QChar character) {
                                 return character.isSpace();
                             });
    return needsQuotes ? QLatin1Char('"') + argument + QLatin1Char('"') : argument;
}

/**
 * @param mode Whether git may take optional locks.
 * @returns The environment for git: the inherited one with a fixed locale.
 */
QProcessEnvironment gitEnvironment(kgt::RunMode mode)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // TortoiseGit sets this too: output we parse must not depend on the user's locale
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    if (mode == kgt::RunMode::ReadOnly) {
        environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    }
    return environment;
}

} // namespace

namespace kgt
{

GitCall::GitCall(const QString& workingDirectory, const QStringList& arguments, RunMode mode, const QByteArray& input)
{
    m_process.setProgram(QLatin1String(gitProgram));
    m_process.setArguments(arguments);
    m_process.setWorkingDirectory(workingDirectory);
    m_process.setProcessEnvironment(gitEnvironment(mode));
    if (input.isEmpty()) {
        m_process.setStandardInputFile(QProcess::nullDevice());
    }

    connect(&m_process, &QProcess::finished, this, &GitCall::onProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // A crash is reported by finished() as well; only a failed start has no finished()
        if (error != QProcess::FailedToStart) {
            return;
        }
        m_stderr = m_process.errorString().toUtf8();
        m_isFinished = true;
        // Possibly still inside the constructor, before anyone could connect: emit from the event loop
        QMetaObject::invokeMethod(this, &GitCall::finished, Qt::QueuedConnection);
    });

    m_process.start();
    if (!input.isEmpty() && m_process.state() != QProcess::NotRunning) {
        // QProcess buffers it and writes it as git reads, from the event loop or from waitForFinished()
        m_process.write(input);
        m_process.closeWriteChannel();
    }
}

GitCall::~GitCall()
{
    // The process would otherwise report its end to this half-destroyed object while QProcess is destroyed
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished();
    }
}

void GitCall::waitForFinished()
{
    if (!m_isFinished) {
        m_process.waitForFinished(-1);
    }
}

bool GitCall::isFinished() const
{
    return m_isFinished;
}

QByteArray GitCall::output() const
{
    Q_ASSERT(m_isFinished);
    const QString stderrText = QString::fromUtf8(m_stderr).trimmed();
    if (m_process.error() == QProcess::FailedToStart) {
        throw RuntimeError({.category = ErrorCategory::Git, .message = QStringLiteral("Could not start %1: %2").arg(commandLine(), stderrText)});
    }
    if (m_process.exitStatus() == QProcess::CrashExit) {
        throw RuntimeError({.category = ErrorCategory::Git, .message = QStringLiteral("%1 crashed: %2").arg(commandLine(), stderrText)});
    }
    if (m_process.exitCode() != 0) {
        throw RuntimeError(
            {.category = ErrorCategory::Git,
             .message = QStringLiteral("%1 failed with exit code %2: %3").arg(commandLine(), QString::number(m_process.exitCode()), stderrText)});
    }
    return m_stdout;
}

QString GitCall::commandLine() const
{
    QStringList parts{QLatin1String(gitProgram)};
    for (const QString& argument : m_process.arguments()) {
        parts.append(quoted(argument));
    }
    return parts.join(QLatin1Char(' '));
}

void GitCall::onProcessFinished()
{
    m_stdout = m_process.readAllStandardOutput();
    m_stderr = m_process.readAllStandardError();
    m_isFinished = true;
    Q_EMIT finished();
}

} // namespace kgt
