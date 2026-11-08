// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/MergeAbortDlg.h (CMergeAbortDlg): the Abort Merge dialog.
 */

#pragma once

#include <QDialog>

class QRadioButton;

namespace kgt
{

/**
 * How a reset to HEAD treats the index and working tree, CAppUtils's resetType numbers.
 */
enum class ResetType {
    Soft = 0, // --soft
    Mixed = 1, // --mixed
    Hard = 2, // --hard
    Merge = 3, // --merge, which MergeAbortDlg's first choice maps to
};

/**
 * Asks how to abort a merge: a reset to HEAD of type merge, mixed or hard.
 */
class MergeAbortDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param workingTree The working tree's root, for the title.
     * @param parent Parent widget.
     */
    explicit MergeAbortDlg(const QString& workingTree, QWidget *parent = nullptr);

    /**
     * @returns The chosen reset, as CAppUtils::MergeAbort maps m_ResetType: Merge, Mixed or Hard.
     */
    [[nodiscard]] ResetType resetType() const;

    /**
     * @param type Merge, Mixed or Hard: the radio button to check.
     */
    void setResetType(ResetType type);

private:
    QRadioButton *m_merge;
    QRadioButton *m_mixed;
    QRadioButton *m_hard;
};

} // namespace kgt
