// SPDX-FileCopyrightText: 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/GitCliOutputParser.cpp

#include "gitclioutputparser.hpp"

#include <QString>

#include <utility>

namespace
{

constexpr qsizetype maxLineLength = qsizetype{8} * 1024;
constexpr qsizetype maxBufferSize = qsizetype{150} * 1024 * 1024;
constexpr QByteArrayView gitRemotePrefix = "remote: ";

bool isEmptyRemoteLine(const QByteArray& line)
{
    return line == gitRemotePrefix;
}

bool isRemoteLine(const QByteArray& line)
{
    return line.startsWith(gitRemotePrefix);
}

/**
 * @param oldLine The line on the console.
 * @param newLine The line written over it after a '\r'.
 * @returns What the console shows: the new line, with the rest of the old one if the new one is shorter.
 */
QByteArray overlayConsoleLine(const QByteArray& oldLine, const QByteArray& newLine)
{
    if (newLine.size() >= oldLine.size()) {
        return newLine;
    }
    QByteArray result = newLine;
    result.append(oldLine.mid(newLine.size()));
    return result;
}

} // namespace

namespace kgt
{

GitCliOutputParser::GitCliOutputParser(qsizetype limit)
    : m_limit(limit)
{
}

void GitCliOutputParser::appendChunk(QByteArrayView chunk)
{
    if (m_dropMode) {
        return;
    }
    for (char character : chunk) {
        if (character == '\0') {
            character = '\n';
        }
        if (character == '\r' || character == '\n') {
            m_inputBuffer.append(character);
            m_inputBufferCurrentLineLength = 0;
            m_inputBufferSkippingTruncatedLine = false;
            continue;
        }
        if (m_inputBufferSkippingTruncatedLine) {
            continue;
        }
        if (m_inputBufferCurrentLineLength >= maxLineLength) {
            m_inputBufferSkippingTruncatedLine = true;
            m_inputBuffer.append(QStringLiteral("... [line truncated at %1 KiB]").arg(maxLineLength / 1024).toUtf8());
            continue;
        }
        ++m_inputBufferCurrentLineLength;
        m_inputBuffer.append(character);
    }

    // A safeguard: the buffer is processed every few milliseconds, so it should never grow this large
    if (m_inputBuffer.size() > maxBufferSize) {
        m_inputBuffer.append(
            QStringLiteral("\n\n[Buffer truncated at about %1 MiB to prevent resource exhaustion]\n").arg(maxBufferSize / 1024 / 1024).toUtf8());
        m_dropMode = true;
    }
}

void GitCliOutputParser::activateDropMode()
{
    m_dropMode = true;
    m_inputBuffer.clear();
}

EmittedLines GitCliOutputParser::processPending()
{
    if (m_inputBuffer.isEmpty()) {
        return {};
    }
    const QByteArray buffer = std::exchange(m_inputBuffer, {});
    return process(buffer);
}

EmittedLines GitCliOutputParser::process(QByteArrayView buffer)
{
    EmittedLines out;
    bool foundCr = false;
    for (const char character : buffer) {
        if (character == '\r' || character == '\n') {
            handleLine(out, m_currentLine, character == '\r');
            foundCr = foundCr || character == '\r';
            m_currentLine.clear();
            if (out.limited) {
                break;
            }
            continue;
        }
        m_currentLine.append(character);
    }

    // Show the latest progress line until its successor arrives. A skipped empty remote line is only a placeholder
    // after a remote LF/CR sequence and may still be replaced by the next real remote line, so it stays hidden
    if (!m_pendingCR.isEmpty() && foundCr && !(m_skipNextEmptyRemoteLF && isEmptyRemoteLine(m_pendingCR))) {
        out.text.append(m_pendingCR);
        m_pendingVisible = true;
    }
    return out;
}

void GitCliOutputParser::reset()
{
    m_currentLine.clear();
    m_pendingCR.clear();
    m_pendingVisible = false;
    m_skipNextEmptyRemoteLF = false;
    m_inputBuffer.clear();
    m_inputBufferSkippingTruncatedLine = false;
    m_inputBufferCurrentLineLength = 0;
    m_dropMode = false;
}

void GitCliOutputParser::appendLine(EmittedLines& out, const QByteArray& line) const
{
    if (out.text.size() > m_limit) {
        out.limited = true;
        return;
    }
    out.text += line;
    out.text += '\n';
}

void GitCliOutputParser::eraseVisiblePendingIfNeeded(EmittedLines& out)
{
    if (!m_pendingVisible) {
        return;
    }
    out.erasePreviousLineWithLength = m_pendingCR.size();
    m_pendingVisible = false;
}

void GitCliOutputParser::discardPendingCR(EmittedLines& out)
{
    eraseVisiblePendingIfNeeded(out);
    m_pendingCR.clear();
}

void GitCliOutputParser::makePendingPermanent(EmittedLines& out)
{
    if (m_pendingCR.isEmpty()) {
        return;
    }
    eraseVisiblePendingIfNeeded(out);
    appendLine(out, m_pendingCR);
    m_pendingCR.clear();
}

void GitCliOutputParser::handleLine(EmittedLines& out, const QByteArray& line, bool endedWithCR)
{
    if (endedWithCR) {
        handleCRLine(out, line);
    } else {
        handleLFLine(out, line);
    }
}

void GitCliOutputParser::handleCRLine(EmittedLines& out, const QByteArray& line)
{
    if (m_pendingCR.isEmpty()) {
        m_pendingCR = line;
        return;
    }
    const bool remoteLine = isRemoteLine(line);
    if (isRemoteLine(m_pendingCR) && remoteLine) {
        const bool emptyRemote = isEmptyRemoteLine(line);
        const bool preserveSkip = m_skipNextEmptyRemoteLF && emptyRemote;
        if (!preserveSkip) {
            m_skipNextEmptyRemoteLF = false;
        }
        if (!emptyRemote) {
            discardPendingCR(out);
        } else if (!m_skipNextEmptyRemoteLF) {
            makePendingPermanent(out);
        }
        m_pendingCR = line;
        return;
    }
    // Normal console CR behaviour
    eraseVisiblePendingIfNeeded(out);
    m_pendingCR = overlayConsoleLine(m_pendingCR, line);
}

void GitCliOutputParser::handleLFLine(EmittedLines& out, const QByteArray& line)
{
    const bool remoteLine = isRemoteLine(line);
    const bool emptyRemote = remoteLine && isEmptyRemoteLine(line);

    // An LF line without a pending CR
    if (m_pendingCR.isEmpty()) {
        if (!m_skipNextEmptyRemoteLF || !emptyRemote) {
            appendLine(out, line);
        }
        m_skipNextEmptyRemoteLF = false;
        return;
    }

    if (isRemoteLine(m_pendingCR) && remoteLine) {
        m_skipNextEmptyRemoteLF = emptyRemote;
        if (emptyRemote && !isEmptyRemoteLine(m_pendingCR)) {
            makePendingPermanent(out);
            return;
        }
        discardPendingCR(out);
        appendLine(out, line);
        return;
    }

    // Normal console LF behaviour after a CR
    eraseVisiblePendingIfNeeded(out);
    if (out.text.size() > m_limit) {
        out.limited = true;
        m_pendingCR.clear();
        m_skipNextEmptyRemoteLF = false;
        return;
    }
    out.text.append(line.isEmpty() ? m_pendingCR : overlayConsoleLine(m_pendingCR, line));
    out.text.append('\n');
    m_pendingCR.clear();
    m_skipNextEmptyRemoteLF = false;
}

} // namespace kgt
