// SPDX-FileCopyrightText: 2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/GitCliOutputParser.h (CGitCliOutputParser): turns git's console output,
 * whose progress lines are redrawn with carriage returns, into text for a log window.
 */

#pragma once

#include <QByteArray>

#include <limits>

namespace kgt
{

/**
 * What one ProcessPending() call adds to the log window, TortoiseGit's EmittedLines.
 */
struct EmittedLines {
    QByteArray text; // Appended to the log, after the erasure below
    qsizetype erasePreviousLineWithLength = 0; // If not 0, the log's last line (a progress line) is removed first
    bool limited = false; // The size limit was reached: the caller stops showing output
};

/**
 * Collects git's output as it arrives and turns it into lines for a log window. A line ended by '\r' is a
 * progress line that the next one replaces, as on a console; it is shown, and then erased once its successor
 * arrives. Git's "remote: " lines, whose CR and LF the remote side mixes, are handled as TortoiseGit does.
 * Unlike TortoiseGit's, this one is used on the UI thread only, so it needs no lock.
 */
class GitCliOutputParser
{
public:
    /**
     * @param limit The size of text after which output is cut off; not an exact limit but a safeguard.
     */
    explicit GitCliOutputParser(qsizetype limit = std::numeric_limits<qsizetype>::max());

    /**
     * Adds output as it was read from git.
     * @param chunk Bytes of git's merged stdout and stderr.
     */
    void appendChunk(QByteArrayView chunk);

    /**
     * Turns the output added since the last call into lines.
     * @returns What to change in the log window.
     */
    [[nodiscard]] EmittedLines processPending();

    /**
     * Drops all further output, once the log window is full.
     */
    void activateDropMode();

    /**
     * Forgets everything, for the next command.
     */
    void reset();

    /**
     * Turns a buffer into lines, as processPending() does with the output collected.
     * @param buffer Output with line lengths already limited.
     * @returns What to change in the log window.
     */
    [[nodiscard]] EmittedLines process(QByteArrayView buffer);

private:
    /**
     * Has the shown progress line erased, if it is shown.
     * @param[in,out] out The changes to the log window.
     */
    void eraseVisiblePendingIfNeeded(EmittedLines& out);

    /**
     * Drops the pending progress line, erasing it if it is shown.
     * @param[in,out] out The changes to the log window.
     */
    void discardPendingCR(EmittedLines& out);

    /**
     * Turns the pending progress line into an ordinary line.
     * @param[in,out] out The changes to the log window.
     */
    void makePendingPermanent(EmittedLines& out);

    /**
     * Handles one complete line.
     * @param[in,out] out The changes to the log window.
     * @param line The line, without its ending.
     * @param endedWithCR Whether it ended with '\r' rather than '\n'.
     */
    void handleLine(EmittedLines& out, const QByteArray& line, bool endedWithCR);

    /**
     * Handles a line ended by '\r': a progress line the next one replaces.
     * @param[in,out] out The changes to the log window.
     * @param line The line, without its ending.
     */
    void handleCRLine(EmittedLines& out, const QByteArray& line);

    /**
     * Handles a line ended by '\n'.
     * @param[in,out] out The changes to the log window.
     * @param line The line, without its ending.
     */
    void handleLFLine(EmittedLines& out, const QByteArray& line);

    /**
     * Appends a line and '\n', unless the size limit is reached.
     * @param[in,out] out The changes to the log window.
     * @param line The line.
     */
    void appendLine(EmittedLines& out, const QByteArray& line) const;

    QByteArray m_inputBuffer;
    bool m_inputBufferSkippingTruncatedLine = false;
    qsizetype m_inputBufferCurrentLineLength = 0;
    qsizetype m_limit;
    bool m_dropMode = false;

    QByteArray m_currentLine;
    QByteArray m_pendingCR; // The last line ended by '\r', which the next line may replace

    bool m_pendingVisible = false; // m_pendingCR is shown as the log's last line
    bool m_skipNextEmptyRemoteLF = false;
};

} // namespace kgt
