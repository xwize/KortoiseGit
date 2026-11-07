// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Plays the role of TortoiseGit src/TortoiseShell/ShellExt.h (CShellExt): the shell integration that adds the
 * context menu and launches kortoisegitproc for every command. Written fresh for Dolphin's plugin API.
 */

#pragma once

#include <Dolphin/KVersionControlPlugin>

#include <QVariantList>

class QAction;

namespace kgt
{

/**
 * The Dolphin version control plugin: TortoiseShell's counterpart. It adds TortoiseGit's context menu entries
 * and starts kortoisegitproc, detached, for each one. No dialog ever runs inside Dolphin's process.
 *
 * Status overlays are not implemented yet: every item reports UnversionedVersion, which Dolphin draws without
 * an overlay.
 */
class ShellExt : public KVersionControlPlugin
{
    Q_OBJECT

public:
    /**
     * @param parent Owner of the plugin (Dolphin's version control observer).
     * @param args Plugin factory arguments; unused. The signature is the one KPluginFactory requires.
     */
    ShellExt(QObject *parent, const QVariantList& args);

    /**
     * @returns ".git", the entry whose presence marks a git working tree.
     */
    [[nodiscard]] QString fileName() const override;

    /**
     * Called by Dolphin on a worker thread before itemVersion() is asked for the items of @p directory.
     * @param directory The directory whose items will be queried.
     * @returns Always true: there is nothing to fetch until overlays are implemented.
     */
    bool beginRetrieval(const QString& directory) override;

    /**
     * Called by Dolphin on a worker thread after the items of a directory have been queried.
     */
    void endRetrieval() override;

    /**
     * @param item The item Dolphin draws.
     * @returns UnversionedVersion for every item, so Dolphin draws no misleading overlay.
     */
    [[nodiscard]] ItemVersion itemVersion(const KFileItem& item) const override;

    /**
     * @param items The selected items, or the current directory when nothing is selected.
     * @returns The context menu actions for items inside a git working tree.
     */
    [[nodiscard]] QList<QAction *> versionControlActions(const KFileItemList& items) const override;

    /**
     * @param items The selected items outside any git working tree.
     * @returns No actions yet; TortoiseGit's Clone and Create repository here will go here.
     */
    [[nodiscard]] QList<QAction *> outOfVersionControlActions(const KFileItemList& items) const override;

private:
    /**
     * Starts kortoisegitproc for the paths stored in @p action's data.
     * @param command TortoiseGit's command name, passed as --command.
     * @param action The triggered action; its data holds the selected paths.
     */
    void runCommand(const QString& command, const QAction *action);

    QAction *m_showLogAction;
};

} // namespace kgt
