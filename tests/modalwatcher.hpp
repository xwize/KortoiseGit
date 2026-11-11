// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Answers the modal windows a test brings up, and reads the follow-ups a ProgressDlg offers.
 */

#pragma once

#include "progressdlg.hpp"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <functional>
#include <tuple>
#include <utility>

namespace kgt
{

using ModalHandler = std::function<bool(QWidget *)>; // Deals with a modal window; returns whether it did

/**
 * Hands every modal window that comes up to a handler until it is destroyed.
 */
class ModalWatcher
{
public:
    /**
     * @param handle Called with the active modal window, over and over until it returns true for it.
     */
    explicit ModalWatcher(ModalHandler handle)
        : m_handle(std::move(handle))
    {
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] {
            if (QWidget *modal = QApplication::activeModalWidget()) {
                std::ignore = m_handle(modal);
            }
        });
        m_timer.start(pollInterval);
    }

private:
    static constexpr int pollInterval = 10;

    ModalHandler m_handle;
    QTimer m_timer;
};

/**
 * @param modal A modal window.
 * @param button A standard button.
 * @returns Whether it was a message box with that button, now pressed.
 */
inline bool pressButton(const QWidget *modal, QDialogButtonBox::StandardButton button)
{
    const auto *buttons = modal->findChild<QDialogButtonBox *>();
    QPushButton *pushButton = buttons ? buttons->button(button) : nullptr;
    if (pushButton) {
        pushButton->click();
        return true;
    }
    return false;
}

/**
 * @param progress A progress dialog whose commands have ended.
 * @returns The follow-ups it offers, in order; none while the button is hidden.
 */
inline QStringList postCmdTexts(const ProgressDlg& progress)
{
    QStringList texts;
    if (progress.postCmdButton()->isHidden()) {
        return texts;
    }
    for (const QAction *action : progress.postCmdButton()->menu()->actions()) {
        texts.append(action->text());
    }
    return texts;
}

/**
 * Chooses a follow-up from a progress dialog's menu button.
 * @param progress A progress dialog whose commands have ended.
 * @param text The follow-up's label.
 * @returns Whether it was offered.
 */
inline bool triggerPostCmd(const ProgressDlg& progress, const QString& text)
{
    for (QAction *action : progress.postCmdButton()->menu()->actions()) {
        if (action->text() == text) {
            action->trigger();
            return true;
        }
    }
    return false;
}

/**
 * Drives a command through its dialogs. A test's own handler sees each modal window first; then, at the end of
 * each ProgressDlg, the driver records the follow-ups offered and chooses the next of its choices, or Close; any
 * other box gets OK or Yes.
 */
class CommandDriver
{
public:
    /**
     * @param choices The follow-up to choose at the end of each progress dialog, in turn; an empty one, or none
     *                left, closes it.
     * @param handle Sees each modal window first; returns true if the window is its own, ready or not, so the
     *               driver leaves it alone.
     */
    explicit CommandDriver(QStringList choices, ModalHandler handle = {})
        : m_choices(std::move(choices))
        , m_handle(std::move(handle))
        , m_watcher([this](QWidget *modal) {
            return answer(modal);
        })
    {
    }

    /**
     * @returns The follow-ups each progress dialog offered, in the order the dialogs ended.
     */
    [[nodiscard]] const QList<QStringList>& offered() const
    {
        return m_offered;
    }

private:
    bool answer(QWidget *modal)
    {
        if (m_handle && m_handle(modal)) {
            return true;
        }
        auto *progress = qobject_cast<ProgressDlg *>(modal);
        if (!progress) {
            return pressButton(modal, QDialogButtonBox::Ok) || pressButton(modal, QDialogButtonBox::Yes);
        }
        if (!progress->isDone() || progress->property(handledProperty).toBool()) {
            return false;
        }
        progress->setProperty(handledProperty, true);
        m_offered.append(postCmdTexts(*progress));
        const QString choice = m_choices.isEmpty() ? QString() : m_choices.takeFirst();
        // Later: a follow-up opens dialogs of its own, which this watcher's timer can't answer from inside its slot
        QTimer::singleShot(0, progress, [progress, choice] {
            if (choice.isEmpty() || !triggerPostCmd(*progress, choice)) {
                progress->accept();
            }
        });
        return true;
    }

    static constexpr const char *handledProperty = "kgtTestHandled";

    QStringList m_choices;
    ModalHandler m_handle;
    QList<QStringList> m_offered;
    ModalWatcher m_watcher;
};

} // namespace kgt
