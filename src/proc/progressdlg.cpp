// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/ProgressDlg.cpp and its IDD_GITPROGRESS resource. The animation, the
 * taskbar progress, the post-command buttons and the log file are left out for now. Git runs through QProcess
 * on the UI thread's event loop instead of a worker thread.
 */

#include "progressdlg.hpp"

#include "git/gitcall.hpp"

#include <KLocalizedString>

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace
{

using namespace std::chrono_literals;

// TortoiseGit's REFRESHTIMER interval
constexpr auto refreshInterval = 125ms;
// TortoiseGit's GitOutputLimitinKiB default: the log window holds at most this much
constexpr qsizetype sizeLimit = qsizetype{2} * 1024 * 1024;
// How long Abort waits for git to end after asking it to, before killing it outright
constexpr int terminateWaitMs = 5000;

/**
 * @param argument One argument.
 * @returns It, quoted if it is empty or holds whitespace, for showing the command line.
 */
QString quoted(const QString& argument)
{
    const bool needsQuotes = argument.isEmpty() || std::any_of(argument.cbegin(), argument.cend(), [](QChar character) {
                                 return character.isSpace();
                             });
    return needsQuotes ? QLatin1Char('"') + argument + QLatin1Char('"') : argument;
}

/**
 * Ported from CProgressDlg::ClearESC: removes the ANSI escape sequences git may write (colours, erase line).
 * @param[in,out] text The text to clean.
 */
void clearEsc(QString& text)
{
    static const QRegularExpression escape(QStringLiteral("\x1b\\[[0-9;]*[mK]"));
    text.remove(escape);
}

/**
 * Ported from CProgressDlg::ParsePercentage.
 * @param line A line holding a percentage.
 * @param percentPosition The position of its '%'.
 * @returns The number before the '%', or 0.
 */
int parsePercentage(const QString& line, qsizetype percentPosition)
{
    qsizetype start = percentPosition;
    while (start > 0 && line.at(start - 1).isDigit()) {
        --start;
    }
    return line.mid(start, percentPosition - start).toInt();
}

} // namespace

namespace kgt
{

ProgressDlg::ProgressDlg(const QString& workingDirectory, QWidget *parent)
    : QDialog(parent)
    , m_currentWork(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_log(new QPlainTextEdit(this))
    , m_closeButton(new QPushButton(i18n("Close"), this))
    , m_abortButton(new QPushButton(i18n("Abort"), this))
    , m_workingDirectory(workingDirectory)
    , m_parser(sizeLimit)
{
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Git Command Progress", QDir::toNativeSeparators(workingDirectory)));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_currentWork);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    layout->addWidget(m_progress);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    // Git aligns its progress columns for a console
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_log, 1);

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(m_closeButton, QDialogButtonBox::AcceptRole);
    buttons->addButton(m_abortButton, QDialogButtonBox::RejectRole);
    m_closeButton->setEnabled(false);
    m_abortButton->setEnabled(false);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_abortButton, &QPushButton::clicked, this, &ProgressDlg::reject);
    layout->addWidget(buttons);

    m_refreshTimer.setInterval(refreshInterval);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ProgressDlg::updateCmdOutput);

    resize(640, 400);
}

ProgressDlg::~ProgressDlg()
{
    // Never leave git running, or let it report to a half-destroyed dialog
    killProcess();
}

void ProgressDlg::addGitCmd(const QStringList& arguments)
{
    Q_ASSERT(!m_started);
    m_gitCmdList.append(arguments);
}

void ProgressDlg::setPostExecCallback(PostExecCallback callback)
{
    Q_ASSERT(!m_started);
    m_postExecCallback = std::move(callback);
}

void ProgressDlg::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    m_elapsed.start();
    m_parser.reset();
    m_abortButton->setEnabled(true);
    m_refreshTimer.start();
    runNext();
}

bool ProgressDlg::isDone() const
{
    return m_done;
}

bool ProgressDlg::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

int ProgressDlg::gitStatus() const
{
    return m_gitStatus;
}

QString ProgressDlg::logText() const
{
    return m_log->toPlainText();
}

QString ProgressDlg::currentWork() const
{
    return m_currentWork->text();
}

void ProgressDlg::reject()
{
    m_abort = true;
    killProcess();
    QDialog::reject();
}

void ProgressDlg::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // Once the dialog is on screen, as TortoiseGit starts its thread at the end of OnInitDialog
    QTimer::singleShot(0, this, &ProgressDlg::start);
}

void ProgressDlg::runNext()
{
    if (m_abort) {
        return;
    }
    if (m_nextCmd >= m_gitCmdList.size()) {
        finish(m_gitCmdList.isEmpty() ? -1 : static_cast<int>(m_combinedStatus));
        return;
    }
    const QStringList arguments = m_gitCmdList.at(m_nextCmd);

    // TortoiseGit's m_bShowCommand, on by default: the command line goes into the log first
    QStringList shown{QStringLiteral("git")};
    for (const QString& argument : arguments) {
        shown.append(quoted(argument));
    }
    m_parser.appendChunk(((m_nextCmd > 0 ? QStringLiteral("\n") : QString()) + shown.join(QLatin1Char(' ')) + QLatin1Char('\n')).toUtf8());
    ++m_nextCmd;

    m_process = std::make_unique<QProcess>();
    m_process->setProgram(QStringLiteral("git"));
    m_process->setArguments(arguments);
    m_process->setWorkingDirectory(m_workingDirectory);
    m_process->setProcessEnvironment(GitCall::environment(RunMode::ReadWrite));
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setStandardInputFile(QProcess::nullDevice());
    // Its own process group, so Abort can kill the ssh and hooks git starts too (TortoiseGit's KillProcessTree)
    m_process->setChildProcessModifier([] {
        ::setpgid(0, 0);
    });
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &ProgressDlg::onReadyRead);
    connect(m_process.get(), &QProcess::finished, this, &ProgressDlg::onProcessFinished);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        m_parser.appendChunk(QByteArray(m_process->errorString().toUtf8() + '\n'));
        finish(-1);
    });
    m_process->start();
}

void ProgressDlg::onReadyRead()
{
    m_parser.appendChunk(m_process->readAllStandardOutput());
}

void ProgressDlg::onProcessFinished()
{
    m_parser.appendChunk(m_process->readAllStandardOutput());
    if (m_process->exitStatus() == QProcess::CrashExit) {
        finish(-1);
        return;
    }
    m_combinedStatus |= static_cast<unsigned int>(m_process->exitCode());
    // TortoiseGit runs the whole list and ORs the exit codes
    QTimer::singleShot(0, this, &ProgressDlg::runNext);
}

void ProgressDlg::updateCmdOutput()
{
    const EmittedLines out = m_parser.processPending();
    if (out.text.isEmpty() && out.erasePreviousLineWithLength == 0) {
        return;
    }
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    if (out.erasePreviousLineWithLength > 0 && !m_log->document()->isEmpty()) {
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    }
    QString text = QString::fromUtf8(out.text);
    clearEsc(text);
    cursor.insertText(text, QTextCharFormat());
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    updateProgressFromLine(text);

    if (out.limited || m_log->document()->characterCount() >= sizeLimit) {
        m_parser.activateDropMode();
        const QString truncated = QStringLiteral("[Output truncated at about %1 KiB]").arg(m_log->document()->characterCount() / 1024);
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral("\n\n...\n") + truncated);
        m_currentWork->setText(truncated);
    }
}

void ProgressDlg::updateProgressFromLine(const QString& text)
{
    // Only the newest line counts; earlier ones in the same batch are outdated
    const QString line = text.trimmed().section(QLatin1Char('\n'), -1);
    const qsizetype colon = line.lastIndexOf(QLatin1Char(':'));
    const qsizetype percent = line.indexOf(QLatin1Char('%'));
    if (colon <= 0 || percent <= 0) {
        return;
    }
    m_currentWork->setText(line.left(colon));
    if (const int position = parsePercentage(line, percent); position > 0) {
        m_progress->setValue(position);
    }
}

void ProgressDlg::finish(int status)
{
    if (m_done) {
        return;
    }
    m_refreshTimer.stop();
    updateCmdOutput();
    m_parser.reset();
    m_done = true;
    m_gitStatus = status;
    m_progress->setValue(100);
    m_closeButton->setEnabled(true);

    if (m_postExecCallback) {
        QString extraMsg;
        m_postExecCallback(m_gitStatus, extraMsg);
        if (!extraMsg.isEmpty()) {
            QTextCursor cursor(m_log->document());
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(extraMsg, QTextCharFormat());
        }
    }

    const QString endTime = QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat);
    const qint64 elapsedMs = m_elapsed.elapsed();
    if (m_gitStatus != 0) {
        const QString message = i18n("git did not exit cleanly (exit code %1)", m_gitStatus);
        m_currentWork->setText(message);
        insertColorText(QStringLiteral("\n\n%1 (%2 ms @ %3)\n").arg(message, QString::number(elapsedMs), endTime), true);
    } else {
        const QString message = i18n("Success");
        m_currentWork->setText(message);
        insertColorText(QStringLiteral("\n%1 (%2 ms @ %3)\n").arg(message, QString::number(elapsedMs), endTime), false);
        m_abortButton->setEnabled(false);
    }
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    m_closeButton->setDefault(true);
    m_closeButton->setFocus();
    Q_EMIT commandsFinished();
}

void ProgressDlg::insertColorText(const QString& text, bool failure)
{
    // TortoiseGit's colours, with its lighter variants on a dark background
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    QColor color;
    if (failure) {
        color = dark ? QColor(207, 47, 47) : QColor(255, 0, 0);
    } else {
        color = dark ? QColor(0, 178, 255) : QColor(0, 0, 255);
    }
    QTextCharFormat format;
    format.setForeground(color);
    format.setFontWeight(QFont::Bold);
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text, format);
}

void ProgressDlg::killProcess()
{
    if (!m_process) {
        return;
    }
    m_process->disconnect(this);
    if (m_process->state() == QProcess::NotRunning) {
        return;
    }
    const auto group = static_cast<pid_t>(-m_process->processId());
    ::kill(group, SIGTERM);
    if (!m_process->waitForFinished(terminateWaitMs)) {
        ::kill(group, SIGKILL);
        m_process->waitForFinished(-1);
    }
}

} // namespace kgt
