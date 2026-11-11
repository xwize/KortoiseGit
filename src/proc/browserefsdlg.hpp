// SPDX-FileCopyrightText: 2009-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/BrowseRefsDlg.h (CBrowseRefsDlg): the "Browse references" dialog, also
 * used as a ref picker by other dialogs: the tree, the list with its columns, the filter, and the list's context menu
 * (log, reflog, compares, fetch, merge, switch, create branch, edit description, rename, tracked branch, deletes and
 * copying), and the tree's menu (manage remotes, fetch, create branch or tag, delete all tags, delete remote tags).
 * Browse repository isn't there: there is no repository browser.
 */

#pragma once

#include "browserefsdlgfilter.hpp"
#include "git/git.hpp"
#include "refleaflistmodel.hpp"
#include "shadowtree.hpp"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>

class QCheckBox;
class QItemSelection;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QStandardItem;
class QStandardItemModel;
class QTreeView;

namespace kgt
{

class GitCall;

/**
 * Which kinds of ref the dialog lists, TortoiseGit's gPickRef_* values.
 */
enum PickRefKind : unsigned int { // NOLINT(cppcoreguidelines-use-enum-class): Combined as bit flags, as in TortoiseGit
    PickRefHead = 1, // gPickRef_Head: local branches
    PickRefTag = 2, // gPickRef_Tag: tags
    PickRefRemote = 4, // gPickRef_Remote: remote-tracking branches
    PickRefAll = PickRefHead | PickRefTag | PickRefRemote, // gPickRef_All
    PickRefNoTag = PickRefAll & ~PickRefTag, // gPickRef_NoTag
};

/**
 * A remote-tracking branch split into its remote and its name there.
 */
struct RemoteBranch {
    QString remote; // Such as origin
    QString branch; // Such as feature/x; empty for the remote itself
};

/**
 * How PickRef sets the dialog up as a picker.
 */
struct PickRefOptions {
    QString initialRef = QStringLiteral("HEAD"); // The ref to select first; HEAD for the current branch
    unsigned int pickRefKind = PickRefAll; // Which kinds of ref to list, PickRefKind bits
    bool pickMultipleRefs = false; // Whether several refs may be picked, returned separated by spaces
};

/**
 * The ref browser: a tree of the repository's refs split at their slashes on the left, and on the right the refs
 * under the selected node with their last commit. Refs load in the background from one git for-each-ref.
 */
class BrowseRefsDlg : public QDialog
{
    Q_OBJECT

public:
    /**
     * Opens the dialog and starts loading the refs.
     * @param workingTree The working tree root or bare repository git runs in.
     * @param parent Parent widget, or nullptr for a top-level dialog.
     */
    explicit BrowseRefsDlg(const QString& workingTree, QWidget *parent = nullptr);
    ~BrowseRefsDlg() override;

    BrowseRefsDlg(const BrowseRefsDlg&) = delete;
    BrowseRefsDlg& operator=(const BrowseRefsDlg&) = delete;
    BrowseRefsDlg(BrowseRefsDlg&&) = delete;
    BrowseRefsDlg& operator=(BrowseRefsDlg&&) = delete;

    /**
     * Ported from CBrowseRefsDlg::PickRef: shows the dialog as a picker and waits for the user.
     * @param workingTree The working tree root or bare repository.
     * @param options What to select first and what may be picked; an empty initialRef means HEAD.
     * @param parent Parent widget.
     * @returns The picked ref's full name, such as refs/heads/main, several short names separated by spaces, or an
     *          empty string if cancelled.
     */
    [[nodiscard]] static QString pickRef(const QString& workingTree, const PickRefOptions& options = {}, QWidget *parent = nullptr);

    /**
     * Turns the dialog into a picker, as PickRef sets it up. Must be called before the refs have loaded.
     * @param options What to select first and what may be picked.
     */
    void setPickMode(const PickRefOptions& options);

    /**
     * @returns The ref chosen when the dialog closed: the selected ref (several separated by spaces), or the
     *          current branch for Current Branch. Empty before the dialog closes or if nothing was selected.
     */
    [[nodiscard]] const QString& pickedRef() const;

    /**
     * Reloads the refs in the background, TortoiseGit's Refresh. refreshed() is emitted when done.
     * @param selectRef The ref or tree node to select then: HEAD for the current branch, or empty to keep the
     *                  selection.
     */
    void refresh(const QString& selectRef = {});

    /**
     * @returns Whether refs are being loaded.
     */
    [[nodiscard]] bool isLoading() const;

    /**
     * Ported from CBrowseRefsDlg::SelectRef: selects a tree node, or a ref in the list under its parent node.
     * @param refName A full name such as refs/heads/main, or, if not exact, a partial one such as main.
     * @param exactMatch Whether @p refName is a full name.
     * @returns Whether something was selected.
     */
    bool selectRef(const QString& refName, bool exactMatch);

    /**
     * Ported from CBrowseRefsDlg::GetSelectedRef.
     * @param onlyIfLeaf Whether only a ref selected in the list counts, not a tree node.
     * @param pickFirstSelIfMultiSel Whether to return the first of several selected refs, rather than all of them.
     * @returns The selected ref's full name, several short names separated by spaces, or an empty string.
     */
    [[nodiscard]] QString selectedRef(bool onlyIfLeaf, bool pickFirstSelIfMultiSel) const;

    /**
     * Applies a filter at once rather than after typing pauses.
     * @param text The filter text.
     * @param selectedFilters Which parts of a ref it looks at, LogFilter bits.
     */
    void setFilter(const QString& text, unsigned int selectedFilters);

    /**
     * @returns The tree of ref name components.
     */
    [[nodiscard]] QTreeView *refTree() const;

    /**
     * @returns The list of refs under the selected tree node.
     */
    [[nodiscard]] QTreeView *refList() const;

    /**
     * @returns The list's model.
     */
    [[nodiscard]] RefLeafListModel *listModel() const;

    /**
     * @returns The text under the list, such as "Showing 2 ref(s), 1 ref(s) selected".
     */
    [[nodiscard]] QString infoText() const;

    /**
     * Builds the context menu for the refs selected in the list, as ShowContextMenu does for them.
     * @returns The menu; its actions do their work when triggered.
     */
    [[nodiscard]] std::unique_ptr<QMenu> createListContextMenu();

    /**
     * Builds the context menu for the selected tree node, as ShowContextMenu does with no refs selected.
     * @returns The menu; its actions do their work when triggered.
     */
    [[nodiscard]] std::unique_ptr<QMenu> createTreeContextMenu();

    /**
     * Ported from CBrowseRefsDlg::GetFullRefName.
     * @param partialRefName A name such as main or origin/main.
     * @returns The full name of the first ref ending in it, or an empty string.
     */
    [[nodiscard]] QString fullRefName(const QString& partialRefName) const;

    /**
     * @param refName A full ref name.
     * @returns The kortoisegitproc arguments that show the log of that ref (TortoiseGit's /range).
     */
    [[nodiscard]] QStringList showLogArguments(const QString& refName) const;

    /**
     * Ported from SplitRemoteBranchName.
     * @param ref A remote-tracking ref, such as refs/remotes/origin/feature/x.
     * @param remotes The repository's remotes.
     * @returns The remote and the branch on it, such as origin and feature/x, or nothing if @p ref is under none
     *          of @p remotes.
     */
    [[nodiscard]] static std::optional<RemoteBranch> splitRemoteBranchName(QString ref, const QStringList& remotes);

    /**
     * Ported from CBrowseRefsDlg::GetTwoSelectedRefs: the last selected ref goes second.
     * @param refNames The two selected refs' full names, in list order.
     * @param lastSelected The full name of the ref selected last.
     * @param separator Such as .. or ...
     * @returns The short names joined, such as main..topic.
     */
    [[nodiscard]] static QString twoSelectedRefs(const QStringList& refNames, const QString& lastSelected, const QString& separator);

    /**
     * Records the picked ref when the dialog closes, as TortoiseGit's OnDestroy.
     * @param result The dialog's result.
     */
    void done(int result) override;

Q_SIGNALS:
    /**
     * The refs have been reloaded (or failed to load) and the tree and list filled.
     */
    void refreshed();

private:
    using VectorShadowTree = QList<const ShadowTree *>;

    void createWidgets();
    void onLoadFinished();
    void populate(const QByteArray& refsOutput, const RefBrowserConfig& config);
    [[nodiscard]] bool isRefKindShown(const QString& refName) const;
    ShadowTree& treeNode(QString refName, ShadowTree *treePos, bool createIfNotExist);
    void fillListCtrlForTreeNode();
    void fillListCtrlForShadowTree(const ShadowTree& tree,
                                   const QString& refNamePrefix,
                                   bool isFirstLevel,
                                   const BrowseRefsDlgFilter& filter,
                                   QList<RefLeafListEntry>& entries);
    [[nodiscard]] const ShadowTree *selectedTreeNode() const;
    [[nodiscard]] VectorShadowTree selectedLeaves() const;
    void updateInfoLabel();
    void setFilterCueText();
    void showFilterMenu();
    void onListContextMenu(const QPoint& pos);
    void onTreeContextMenu(const QPoint& pos);
    [[nodiscard]] std::unique_ptr<QMenu> createTreeContextMenu(const ShadowTree *tree);
    void addTreeTagEntries(QMenu& popupMenu);
    void manageRemotes();
    void createTag();
    void deleteAllTags();
    void deleteRemoteTags(const QString& remote);
    void onListDoubleClicked();
    void showLog(const QString& refName) const;
    void deleteRefs(const QStringList& refNames);
    [[nodiscard]] bool confirmDeleteRef(const QStringList& refNames);
    bool doDeleteRefs(const QStringList& refNames);
    bool doDeleteRef(const QString& completeRefName);
    bool deleteRemoteRefs(const QString& remote, const QStringList& branches);
    void onCurrentBranchClicked();
    void onListSelectionChanged(const QItemSelection& selected);
    void addSingleRefEntries(QMenu& popupMenu, const ShadowTree& leaf);
    void addTwoRefEntries(QMenu& popupMenu, const VectorShadowTree& leaves);
    void showRefLog(const QString& refName);
    void compareWithWorkingTree(const QString& refName);
    void fetchFrom(const QString& remote);
    void mergeRef(const QString& refName);
    void switchToRef(const QString& refName);
    void createBranch(const QString& hash);
    void editBranchDescription(const QString& branch);
    void startRename(const QString& refName);
    void onNameEdited(const QString& refName, const QString& newText);
    void dropUpstream(const QString& branch);
    void setUpstream(const QString& branch);
    void compareRefs(const QString& refName1, const QString& refName2);

    Git m_git;
    QString m_currentBranch; // The current branch's name, "HEAD" when detached, or empty if unknown
    QStringList m_remotes;
    ShadowTree m_treeRoot;
    const ShadowTree *m_listCtrlRoot = nullptr; // The tree node whose refs the list shows
    QHash<const ShadowTree *, QStandardItem *> m_treeItems; // CShadowTree::m_hTree
    QHash<const QStandardItem *, const ShadowTree *> m_treeNodes; // The tree control's item data
    std::unique_ptr<GitCall> m_refsCall;
    std::unique_ptr<GitCall> m_configCall;
    QString m_pendingSelectRef; // What to select once the running load ends
    QString m_lastSelected; // m_sLastSelected: the full name of the ref selected last in the list
    bool m_hasWC = true; // m_bHasWC: false for a bare repository

    QLineEdit *m_filter = nullptr;
    QComboBox *m_branchFilter = nullptr;
    QTreeView *m_refTree = nullptr;
    QStandardItemModel *m_treeModel = nullptr;
    QTreeView *m_listRefLeafs = nullptr;
    RefLeafListModel *m_listModel = nullptr;
    QCheckBox *m_includeNestedRefs = nullptr;
    QLabel *m_infoLabel = nullptr;
    QTimer m_filterTimer; // IDT_FILTER: applies the filter a second after typing stops
    unsigned int m_selectedFilters = LogFilterAll;

    QString m_initialRef;
    unsigned int m_pickRefKind = PickRefAll;
    QString m_pickedRef;
    bool m_wantPick = false;
    bool m_pickOne = false;
    bool m_pickedRefSet = false;
};

} // namespace kgt
