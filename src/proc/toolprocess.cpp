// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "toolprocess.hpp"

#include <KLocalizedString>
#include <KMessageBox>

#include <QApplication>
#include <QEventLoop>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

namespace
{

using namespace kgt;

/**
 * @param mode Which git command it is.
 * @returns The setting that names its tool.
 */
QString toolSetting(ToolMode mode)
{
    return mode == ToolMode::Diff ? QStringLiteral("diff.tool") : QStringLiteral("merge.tool");
}

} // namespace

namespace kgt
{

ToolProcess::ToolProcess(ToolMode mode, const QString& workingDirectory, const QStringList& arguments, QObject *parent)
    : QObject(parent)
    , m_mode(mode)
    , m_process(new QProcess(this))
{
    m_process->setProgram(QStringLiteral("git"));
    m_process->setArguments(arguments);
    m_process->setWorkingDirectory(workingDirectory);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    if (m_output.open()) {
        m_process->setStandardOutputFile(m_output.fileName());
    }
    // End-of-file answers git mergetool's "Was the merge successful?" with no, and makes Vim exit
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        onFinished(exitCode, status == QProcess::CrashExit);
    });
}

bool ToolProcess::start()
{
    m_process->start();
    return m_process->waitForStarted();
}

QString ToolProcess::configError(ToolMode mode, const QString& configuredTool, const AppUtils::IsInstalled& isInstalled)
{
    if (configuredTool.isEmpty()) {
        // GitDiff::withDefaultTool and AppUtils::mergeToolArguments choose one of these
        const QStringList candidates{QStringLiteral("meld"), QStringLiteral("kdiff3")};
        if (std::ranges::none_of(candidates, isInstalled)) {
            return i18n("%1 is not set, and neither KDiff3 nor Meld is installed.", toolSetting(mode));
        }
        return {};
    }
    if (isTerminalTool(configuredTool)) {
        return i18n("%1 runs in a terminal, which KortoiseGit can't show. Set %2 to a graphical tool such as KDiff3 or Meld.",
                    configuredTool,
                    toolSetting(mode));
    }
    return {};
}

bool ToolProcess::isTerminalTool(const QString& tool)
{
    // The terminal variants among git's mergetools/vimdiff; gvimdiff opens a window
    static const QRegularExpression terminalTools(QStringLiteral("^n?vimdiff[1-3]?$"));
    return terminalTools.match(tool).hasMatch();
}

bool ToolProcess::toolFailed(ToolMode mode, int exitCode, const QString& output)
{
    if (exitCode == 0) {
        return false;
    }
    // Printed untranslated, perhaps after a prompt, only when the tool ran and left the file unresolved (merge_file in
    // git-mergetool). Any other failure is git's own: no usable tool, or a prompt for a tool it guessed
    static const QRegularExpression unresolved(QStringLiteral("merge of .+ failed$"), QRegularExpression::MultilineOption);
    return mode == ToolMode::Diff || !unresolved.match(output).hasMatch();
}

QString ToolProcess::failureMessage(ToolMode mode, const QString& detail)
{
    // IDS_ERR_EXTDIFFSTART and IDS_ERR_EXTMERGESTART
    return mode == ToolMode::Diff ? i18n("Could not start external diff program!\n\n%1", detail)
                                  : i18n("Could not start external merge program!\n\n%1", detail);
}

bool ToolProcess::launch(QWidget *parent, ToolMode mode, const QString& workingDirectory, const QStringList& arguments)
{
    // Owned by the application, not the dialog, so closing the dialog doesn't end the tool
    auto *tool = new ToolProcess(mode, workingDirectory, arguments, qApp);
    const QPointer<QWidget> messageParent(parent);
    connect(tool, &ToolProcess::failed, tool, [messageParent](const QString& message) {
        KMessageBox::error(messageParent, message, QString(), KMessageBox::Options());
    });
    connect(tool, &ToolProcess::finished, tool, &QObject::deleteLater);
    if (tool->start()) {
        return true;
    }
    tool->deleteLater();
    const QString command = mode == ToolMode::Diff ? QStringLiteral("git difftool") : QStringLiteral("git mergetool");
    KMessageBox::error(parent, failureMessage(mode, command), QString(), KMessageBox::Options());
    return false;
}

void ToolProcess::waitForAll()
{
    const QList<ToolProcess *> tools = qApp->findChildren<ToolProcess *>(Qt::FindDirectChildrenOnly);
    if (tools.isEmpty()) {
        return;
    }
    // Closing a failure message would otherwise quit the loop while other tools still run
    const bool quitOnLastWindowClosed = QApplication::quitOnLastWindowClosed();
    QApplication::setQuitOnLastWindowClosed(false);
    QEventLoop loop;
    auto running = tools.size();
    for (const ToolProcess *tool : tools) {
        connect(tool, &QObject::destroyed, &loop, [&loop, &running] {
            if (--running == 0) {
                loop.quit();
            }
        });
    }
    loop.exec();
    QApplication::setQuitOnLastWindowClosed(quitOnLastWindowClosed);
}

void ToolProcess::onFinished(int exitCode, bool crashed)
{
    const QString output = QString::fromLocal8Bit(m_output.readAll()).trimmed();
    if (crashed || toolFailed(m_mode, exitCode, output)) {
        Q_EMIT failed(failureMessage(m_mode, output));
    }
    Q_EMIT finished();
}

} // namespace kgt
