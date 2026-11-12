// SPDX-FileCopyrightText: 2009-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/BrowseRefsDlg.cpp and its IDD_BROWSE_REFS resource

#include "browserefsdlg.hpp"

#include "apputils.hpp"
#include "browserefsdlgfilter.hpp"
#include "commands/fetchcommand.hpp"
#include "commands/mergecommand.hpp"
#include "createbranchtagdlg.hpp"
#include "deleteremotetagdlg.hpp"
#include "gitdiff.hpp"
#include "progressdlg.hpp"
#include "reflogdlg.hpp"

#include "git/gitadmindir.hpp"
#include "git/gitcall.hpp"
#include "git/gitrevrefbrowser.hpp"
#include "utils/runtimeerror.hpp"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelection>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <tuple>

namespace
{

using namespace kgt;
using namespace Qt::StringLiterals;

constexpr auto headsPrefix = "refs/heads/"_L1;
constexpr auto remotesPrefix = "refs/remotes/"_L1;
constexpr auto tagsPrefix = "refs/tags/"_L1;
constexpr int filterDelay = 1000; // IDT_FILTER's timeout in ms
constexpr int treeWidth = 180;
constexpr QSize initialSize(900, 480);
// Like TortoiseGit's message boxes, these don't also raise a desktop notification
constexpr KMessageBox::Options noNotify = KMessageBox::Options();

/**
 * @param workingTree The working tree root or bare repository.
 * @returns Its current branch, from HEAD, as CGit::GetCurrentBranchFromFile: "HEAD" if detached, empty if unknown.
 */
QString currentBranchOf(const QString& workingTree)
{
    QString adminDir = GitAdminDir::worktreeAdminDirPath(workingTree);
    if (adminDir.isEmpty() && GitAdminDir::isBareRepo(workingTree)) {
        adminDir = workingTree;
    }
    return adminDir.isEmpty() ? QString() : GitAdminDir::currentBranchFromFile(adminDir);
}

/**
 * @param leaves Refs.
 * @param from A prefix such as refs/heads/.
 * @returns Whether every ref is under it, CBrowseRefsDlg::AreAllFrom.
 */
bool areAllFrom(const QStringList& refNames, QLatin1StringView from)
{
    return std::ranges::all_of(refNames, [from](const QString& refName) {
        return refName.startsWith(from);
    });
}

/**
 * @param call A finished git call.
 * @returns Its output, or an empty one if git exited with 1 (git config's "nothing found").
 * @throws RuntimeError for any other failure.
 */
QByteArray configOutput(const GitCall& call)
{
    try {
        return call.output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: no remotes and no descriptions. Anything worse shows up in for-each-ref too
        return {};
    }
}

/**
 * Asks a yes/no question as TortoiseGit's MessageBox with MB_YESNO.
 * @param parent The dialog.
 * @param question The question.
 * @param warning Whether to show it as a warning (MB_ICONWARNING) rather than a question.
 * @returns Whether the user answered Yes.
 */
bool askYesNo(QWidget *parent, const QString& question, bool warning)
{
    const KGuiItem yesItem(i18n("&Yes"));
    const KGuiItem noItem(i18n("&No"));
    const auto answer = warning ? KMessageBox::warningTwoActions(parent, question, QString(), yesItem, noItem, QString(), noNotify)
                                : KMessageBox::questionTwoActions(parent, question, QString(), yesItem, noItem, QString(), noNotify);
    return answer == KMessageBox::PrimaryAction;
}

} // namespace

namespace kgt
{

BrowseRefsDlg::BrowseRefsDlg(const QString& workingTree, QWidget *parent)
    : QDialog(parent)
    , m_git(workingTree)
    , m_hasWC(!GitAdminDir::isBareRepo(workingTree))
    , m_initialRef(QStringLiteral("HEAD"))
{
    createWidgets();
    // CAppUtils::SetWindowTitle
    setWindowTitle(i18nc("@title:window %1 is a folder", "%1 - Browse references", QDir::toNativeSeparators(workingTree)));
    // OnInitDialog runs Refresh(m_initialRef); deferred here so that setPickMode() can come first
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (!m_refsCall && m_treeItems.isEmpty()) {
                refresh(m_initialRef);
            }
        },
        Qt::QueuedConnection);
}

BrowseRefsDlg::~BrowseRefsDlg()
{
    // The list points into the tree
    m_listModel->setEntries({});
}

QString BrowseRefsDlg::pickRef(const QString& workingTree, const PickRefOptions& options, QWidget *parent)
{
    BrowseRefsDlg dlg(workingTree, parent);
    PickRefOptions pickOptions = options;
    if (pickOptions.initialRef.isEmpty()) {
        pickOptions.initialRef = QStringLiteral("HEAD");
    }
    dlg.setPickMode(pickOptions);
    if (dlg.exec() != QDialog::Accepted) {
        return {};
    }
    return dlg.pickedRef();
}

void BrowseRefsDlg::setPickMode(const PickRefOptions& options)
{
    m_wantPick = true;
    m_initialRef = options.initialRef;
    m_pickRefKind = options.pickRefKind;
    m_pickOne = !options.pickMultipleRefs;
    if (m_pickOne) {
        m_listRefLeafs->setSelectionMode(QAbstractItemView::SingleSelection);
    }
}

const QString& BrowseRefsDlg::pickedRef() const
{
    return m_pickedRef;
}

void BrowseRefsDlg::createWidgets()
{
    auto *layout = new QVBoxLayout(this);

    auto *filterRow = new QHBoxLayout;
    m_filter = new QLineEdit(this);
    m_filter->setClearButtonEnabled(true);
    const QAction *filterMenu = m_filter->addAction(QIcon::fromTheme(QStringLiteral("view-filter")), QLineEdit::LeadingPosition);
    connect(filterMenu, &QAction::triggered, this, &BrowseRefsDlg::showFilterMenu);
    auto *filterLabel = new QLabel(i18n("Filter:"), this);
    filterLabel->setBuddy(m_filter);
    m_branchFilter = new QComboBox(this);
    m_branchFilter->addItems({i18n("All"), i18n("Only merged (to HEAD)"), i18n("Only unmerged (to HEAD)")});
    filterRow->addSpacing(treeWidth);
    filterRow->addWidget(filterLabel);
    filterRow->addWidget(m_filter, 1);
    filterRow->addWidget(m_branchFilter, 1);
    layout->addLayout(filterRow);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    m_treeModel = new QStandardItemModel(this);
    m_refTree = new QTreeView(splitter);
    m_refTree->setModel(m_treeModel);
    m_refTree->setHeaderHidden(true);
    m_refTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_refTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listModel = new RefLeafListModel(this);
    m_listRefLeafs = new QTreeView(splitter);
    m_listRefLeafs->setModel(m_listModel);
    m_listRefLeafs->setRootIsDecorated(false);
    m_listRefLeafs->setUniformRowHeights(true);
    m_listRefLeafs->setAllColumnsShowFocus(true);
    m_listRefLeafs->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listRefLeafs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listRefLeafs->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listRefLeafs->header()->setSortIndicator(RefLeafListModel::NameColumn, Qt::AscendingOrder);
    m_listRefLeafs->setSortingEnabled(true);
    m_listRefLeafs->setColumnWidth(RefLeafListModel::MsgColumn, 300);
    splitter->setSizes({treeWidth, initialSize.width() - treeWidth});
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    auto *bottomRow = new QHBoxLayout;
    m_includeNestedRefs = new QCheckBox(i18n("Show &nested refs"), this);
    m_includeNestedRefs->setChecked(true);
    m_infoLabel = new QLabel(this);
    m_infoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    // TortoiseGit's order: Current Branch, OK, Cancel
    auto *currentBranch = new QPushButton(i18n("Current Branch"), this);
    currentBranch->setAutoDefault(false);
    bottomRow->addWidget(m_includeNestedRefs);
    bottomRow->addWidget(m_infoLabel, 1);
    bottomRow->addWidget(currentBranch);
    bottomRow->addWidget(buttons);
    layout->addLayout(bottomRow);

    m_filterTimer.setSingleShot(true);
    m_filterTimer.setInterval(filterDelay);
    connect(&m_filterTimer, &QTimer::timeout, this, &BrowseRefsDlg::fillListCtrlForTreeNode);
    connect(m_filter, &QLineEdit::textChanged, &m_filterTimer, qOverload<>(&QTimer::start));
    connect(m_branchFilter, &QComboBox::currentIndexChanged, this, [this] {
        refresh();
    });
    connect(m_includeNestedRefs, &QCheckBox::toggled, this, [this] {
        refresh();
    });
    connect(m_refTree->selectionModel(), &QItemSelectionModel::currentChanged, this, &BrowseRefsDlg::fillListCtrlForTreeNode);
    connect(m_listRefLeafs->selectionModel(), &QItemSelectionModel::selectionChanged, this, &BrowseRefsDlg::onListSelectionChanged);
    // Queued: the rename runs git and may show a message box, which mustn't happen inside the editor's commit
    connect(m_listModel, &RefLeafListModel::nameEdited, this, &BrowseRefsDlg::onNameEdited, Qt::QueuedConnection);
    connect(m_listRefLeafs, &QWidget::customContextMenuRequested, this, &BrowseRefsDlg::onListContextMenu);
    connect(m_refTree, &QWidget::customContextMenuRequested, this, &BrowseRefsDlg::onTreeContextMenu);
    connect(m_listRefLeafs, &QAbstractItemView::doubleClicked, this, &BrowseRefsDlg::onListDoubleClicked);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(currentBranch, &QPushButton::clicked, this, &BrowseRefsDlg::onCurrentBranchClicked);

    // PreTranslateMessage's keys
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, [this] {
        refresh();
    });
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), this), &QShortcut::activated, this, [this] {
        m_filter->selectAll();
        m_filter->setFocus();
    });

    setFilterCueText();
    updateInfoLabel();
    resize(initialSize);
    m_listRefLeafs->setFocus();
}

void BrowseRefsDlg::refresh(const QString& selectRef)
{
    m_currentBranch = currentBranchOf(m_git.workingDirectory());
    QString target = selectRef;
    if (target == "HEAD"_L1) {
        target = m_currentBranch.isEmpty() || m_currentBranch == "HEAD"_L1 ? QStringLiteral("refs/heads") : headsPrefix + m_currentBranch;
    } else if (target.isEmpty()) {
        target = selectedRef(false, true);
    }
    m_pendingSelectRef = target;

    const auto mergeFilter = static_cast<RefMergeFilter>(qMax(0, m_branchFilter->currentIndex()));
    m_refsCall = m_git.runAsync(GitRevRefBrowser::forEachRefArguments(mergeFilter), RunMode::ReadOnly);
    m_configCall = m_git.runAsync(GitRevRefBrowser::configArguments(), RunMode::ReadOnly);
    connect(m_refsCall.get(), &GitCall::finished, this, &BrowseRefsDlg::onLoadFinished);
    connect(m_configCall.get(), &GitCall::finished, this, &BrowseRefsDlg::onLoadFinished);
}

bool BrowseRefsDlg::isLoading() const
{
    return m_refsCall != nullptr;
}

void BrowseRefsDlg::onLoadFinished()
{
    if (!m_refsCall || !m_configCall || !m_refsCall->isFinished() || !m_configCall->isFinished()) {
        return;
    }
    // Deleted from the event loop: one of them is still emitting
    GitCall *refsCall = m_refsCall.release();
    GitCall *configCall = m_configCall.release();
    refsCall->deleteLater();
    configCall->deleteLater();

    QByteArray refsOutput;
    try {
        refsOutput = refsCall->output();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, i18n("Get refs failed:") + e.error().message, QString(), noNotify);
    }
    populate(refsOutput, GitRevRefBrowser::parseConfig(configOutput(*configCall)));
    Q_EMIT refreshed();
}

bool BrowseRefsDlg::isRefKindShown(const QString& refName) const
{
    // Refresh's filter callback: use refs based on m_pickRef_Kind
    if (refName.startsWith(headsPrefix) && !(m_pickRefKind & PickRefHead)) {
        return false;
    }
    if (refName.startsWith(tagsPrefix) && !(m_pickRefKind & PickRefTag)) {
        return false;
    }
    if (refName.startsWith(remotesPrefix) && !(m_pickRefKind & PickRefRemote)) {
        return false;
    }
    // Don't show refs/stash if only remote branches are requested
    return m_pickRefKind != PickRefRemote || refName.startsWith(remotesPrefix);
}

void BrowseRefsDlg::populate(const QByteArray& refsOutput, const RefBrowserConfig& config)
{
    m_remotes = config.remotes;
    Result<QMap<QString, GitRevRefBrowser>> refMap = GitRevRefBrowser::parseRefMap(refsOutput, config.descriptions);
    if (!refMap) {
        KMessageBox::error(this, i18n("Get refs failed:") + refMap.error().message, QString(), noNotify);
        refMap = QMap<QString, GitRevRefBrowser>();
    }

    // The list points into the tree, which is about to be rebuilt
    m_listModel->setEntries({});
    m_listCtrlRoot = nullptr;
    m_treeItems.clear();
    m_treeNodes.clear();
    m_treeModel->clear();
    m_treeRoot = ShadowTree{.name = QStringLiteral("refs"), .ref = {}, .children = {}, .parent = nullptr};
    auto *rootItem = new QStandardItem(QStringLiteral("refs"));
    m_treeModel->appendRow(rootItem);
    m_treeItems.insert(&m_treeRoot, rootItem);
    m_treeNodes.insert(rootItem, &m_treeRoot);

    bool anyShown = false;
    for (auto it = refMap->cbegin(); it != refMap->cend(); ++it) {
        if (!isRefKindShown(it.key())) {
            continue;
        }
        treeNode(it.key(), nullptr, true).ref = it.value();
        anyShown = true;
    }
    m_listModel->setCurrentBranchRef(m_currentBranch.isEmpty() || m_currentBranch == "HEAD"_L1 ? QString() : headsPrefix + m_currentBranch);

    // Always expand the tree first
    m_refTree->expand(rootItem->index());
    // Try an exact match first
    if (!m_pendingSelectRef.isEmpty() && !selectRef(m_pendingSelectRef, true)) {
        selectRef(m_pendingSelectRef, false);
    } else if (!anyShown) {
        selectRef(QStringLiteral("refs"), false);
    }
    m_pendingSelectRef.clear();
}

ShadowTree& BrowseRefsDlg::treeNode(QString refName, ShadowTree *treePos, bool createIfNotExist)
{
    if (!treePos) {
        if (refName.startsWith("refs/"_L1)) {
            refName = refName.mid(5);
        }
        treePos = &m_treeRoot;
    }
    if (refName.isEmpty()) {
        return *treePos;
    }
    ShadowTree *nextTree = treePos->nextSub(refName, createIfNotExist);
    if (!nextTree) {
        // Should not occur when all ref names are valid and createIfNotExist is true
        Q_ASSERT(!createIfNotExist);
        return *treePos;
    }
    if (!refName.isEmpty() && !m_treeItems.contains(nextTree)) {
        // Not a leaf: the leaves are for the list
        auto *item = new QStandardItem(nextTree->name);
        m_treeItems.value(treePos)->appendRow(item);
        m_treeItems.insert(nextTree, item);
        m_treeNodes.insert(item, nextTree);
    }
    return treeNode(refName, nextTree, createIfNotExist);
}

bool BrowseRefsDlg::selectRef(const QString& refName, bool exactMatch)
{
    QString name = refName;
    if (!exactMatch) {
        const QString fullName = fullRefName(name);
        // If empty, name is not a valid ref: select as well as possible
        if (!fullName.isEmpty()) {
            name = fullName;
        }
    }
    if (!name.startsWith("refs"_L1)) {
        return false;
    }
    const ShadowTree& node = treeNode(name, nullptr, false);
    if (const QStandardItem *item = m_treeItems.value(&node)) {
        // Not a leaf: select the tree node
        m_refTree->setCurrentIndex(item->index());
        return true;
    }
    if (!node.parent) {
        return false;
    }
    m_refTree->setCurrentIndex(m_treeItems.value(node.parent)->index());
    fillListCtrlForTreeNode();
    for (int row = 0; row < m_listModel->rowCount(); ++row) {
        if (m_listModel->entry(row).tree == &node) {
            const QModelIndex index = m_listModel->index(row, 0);
            m_listRefLeafs->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            m_listRefLeafs->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
            m_listRefLeafs->scrollTo(index);
        }
    }
    return true;
}

const ShadowTree *BrowseRefsDlg::selectedTreeNode() const
{
    const QStandardItem *item = m_treeModel->itemFromIndex(m_refTree->currentIndex());
    return item ? m_treeNodes.value(item) : nullptr;
}

void BrowseRefsDlg::fillListCtrlForTreeNode()
{
    m_filterTimer.stop();
    const ShadowTree *tree = selectedTreeNode();
    QList<RefLeafListEntry> entries;
    if (tree) {
        const BrowseRefsDlgFilter filter(m_filter->text(), false, m_selectedFilters, false);
        fillListCtrlForShadowTree(*tree, QString(), true, filter, entries);
    }
    m_listModel->setEntries(entries);
    const bool isHeads = tree && tree->isFrom("refs/heads"_L1);
    m_listRefLeafs->setColumnHidden(RefLeafListModel::UpstreamColumn, !isHeads);
    m_listRefLeafs->setColumnHidden(RefLeafListModel::DescriptionColumn, !isHeads);
    for (int column = 0; column < RefLeafListModel::ColumnCount; ++column) {
        if (column != RefLeafListModel::MsgColumn) {
            m_listRefLeafs->resizeColumnToContents(column);
        }
    }
    updateInfoLabel();
}

void BrowseRefsDlg::fillListCtrlForShadowTree(const ShadowTree& tree,
                                              const QString& refNamePrefix,
                                              bool isFirstLevel,
                                              const BrowseRefsDlgFilter& filter,
                                              QList<RefLeafListEntry>& entries)
{
    if (tree.isLeaf()) {
        const QString ref = refNamePrefix + tree.name;
        if (!(tree.name.isEmpty() || (tree.name == "refs"_L1 && !tree.parent)) && filter(tree, ref)) {
            entries.append({.tree = &tree, .name = ref});
        }
        if (isFirstLevel) {
            m_listCtrlRoot = &tree;
        }
        return;
    }
    QString thisName;
    if (!isFirstLevel && !m_includeNestedRefs->isChecked()) {
        return;
    }
    if (isFirstLevel) {
        m_listCtrlRoot = &tree;
    } else {
        thisName = refNamePrefix + tree.name + QLatin1Char('/');
    }
    for (const auto& [childName, child] : tree.children) {
        fillListCtrlForShadowTree(child, thisName, false, filter, entries);
    }
}

QString BrowseRefsDlg::selectedRef(bool onlyIfLeaf, bool pickFirstSelIfMultiSel) const
{
    const VectorShadowTree leaves = selectedLeaves();
    if (!leaves.isEmpty() && (pickFirstSelIfMultiSel || leaves.size() == 1)) {
        return leaves.constFirst()->refName();
    }
    if (!leaves.isEmpty()) {
        // Several refs are selected
        QStringList refs;
        for (const ShadowTree *leaf : leaves) {
            QString ref = leaf->refName();
            if (ref.startsWith("refs/"_L1)) {
                ref = ref.mid(5);
            }
            if (ref.startsWith("heads/"_L1)) {
                ref = ref.mid(6);
            }
            refs.append(ref);
        }
        return refs.join(QLatin1Char(' '));
    }
    if (!onlyIfLeaf) {
        if (const ShadowTree *tree = selectedTreeNode()) {
            return tree->refName();
        }
    }
    return {};
}

BrowseRefsDlg::VectorShadowTree BrowseRefsDlg::selectedLeaves() const
{
    // In list order, as GetFirstSelectedItemPosition walks them
    QModelIndexList rows = m_listRefLeafs->selectionModel()->selectedRows();
    std::ranges::sort(rows, {}, &QModelIndex::row);
    VectorShadowTree leaves;
    for (const QModelIndex& index : rows) {
        leaves.append(m_listModel->entry(index.row()).tree);
    }
    return leaves;
}

void BrowseRefsDlg::setFilter(const QString& text, unsigned int selectedFilters)
{
    m_selectedFilters = selectedFilters;
    setFilterCueText();
    {
        const QSignalBlocker blocker(m_filter);
        m_filter->setText(text);
    }
    fillListCtrlForTreeNode();
}

QTreeView *BrowseRefsDlg::refTree() const
{
    return m_refTree;
}

QTreeView *BrowseRefsDlg::refList() const
{
    return m_listRefLeafs;
}

RefLeafListModel *BrowseRefsDlg::listModel() const
{
    return m_listModel;
}

QString BrowseRefsDlg::infoText() const
{
    return m_infoLabel->text();
}

void BrowseRefsDlg::onListSelectionChanged(const QItemSelection& selected)
{
    // OnItemChangedListRefLeafs
    for (const QModelIndex& index : selected.indexes()) {
        if (index.column() == RefLeafListModel::NameColumn) {
            m_lastSelected = m_listModel->entry(index.row()).tree->refName();
        }
    }
    updateInfoLabel();
}

void BrowseRefsDlg::updateInfoLabel()
{
    m_infoLabel->setText(i18n("Showing %1 ref(s), %2 ref(s) selected", m_listModel->rowCount(), m_listRefLeafs->selectionModel()->selectedRows().size()));
}

void BrowseRefsDlg::setFilterCueText()
{
    QStringList parts;
    if (m_selectedFilters & LogFilterRefName) {
        parts.append(i18n("Refname"));
    }
    if (m_selectedFilters & LogFilterSubject) {
        parts.append(i18n("Subject"));
    }
    if (m_selectedFilters & LogFilterAuthors) {
        parts.append(i18n("Authors"));
    }
    if (m_selectedFilters & LogFilterRevs) {
        parts.append(i18n("SHA-1"));
    }
    m_filter->setPlaceholderText(i18n("Filter by") + QLatin1Char(' ') + parts.join(QStringLiteral(", ")));
}

void BrowseRefsDlg::showFilterMenu()
{
    // OnClickedInfoIcon
    QMenu popup(this);
    const auto addFilter = [&](const QString& text, unsigned int bit) {
        QAction *action = popup.addAction(text);
        action->setCheckable(true);
        action->setChecked(m_selectedFilters & bit);
        action->setData(bit);
    };
    addFilter(i18n("Refname"), LogFilterRefName);
    addFilter(i18n("Subject"), LogFilterSubject);
    addFilter(i18n("Authors"), LogFilterAuthors);
    addFilter(i18n("SHA-1"), LogFilterRevs);
    popup.addSeparator();
    popup.addAction(i18n("Toggle filters"))->setData(static_cast<unsigned int>(LogFilterToggle));
    const QAction *selection = popup.exec(m_filter->mapToGlobal(QPoint(0, m_filter->height())));
    if (!selection) {
        return;
    }
    const unsigned int bit = selection->data().toUInt();
    if (bit == LogFilterToggle) {
        m_selectedFilters = ~m_selectedFilters & LogFilterAll;
    } else {
        m_selectedFilters ^= bit;
    }
    setFilterCueText();
    m_filterTimer.start();
}

std::unique_ptr<QMenu> BrowseRefsDlg::createListContextMenu()
{
    auto popupMenu = std::make_unique<QMenu>(this);
    const VectorShadowTree leaves = selectedLeaves();
    // By name: a refresh finishing while the menu is open rebuilds the tree the leaves are in
    QStringList refNames;
    for (const ShadowTree *leaf : leaves) {
        refNames.append(leaf->refName());
    }
    bool addSeparator = false;
    if (leaves.size() == 1) {
        if (m_wantPick) {
            connect(popupMenu->addAction(i18n("Select")), &QAction::triggered, this, &QDialog::accept);
            popupMenu->addSeparator();
        }
        addSingleRefEntries(*popupMenu, *leaves.constFirst());
        addSeparator = true;
    } else if (leaves.size() == 2) {
        addTwoRefEntries(*popupMenu, leaves);
        addSeparator = true;
    }

    QString deleteText;
    if (!leaves.isEmpty() && areAllFrom(refNames, remotesPrefix)) {
        deleteText = leaves.size() == 1 ? i18n("Delete remote branch") : i18n("Delete %1 remote branches", leaves.size());
    } else if (!leaves.isEmpty() && areAllFrom(refNames, headsPrefix)) {
        deleteText = leaves.size() == 1 ? i18n("Delete branch") : i18n("Delete %1 branches", leaves.size());
    } else if (!leaves.isEmpty() && areAllFrom(refNames, tagsPrefix)) {
        deleteText = leaves.size() == 1 ? i18n("Delete tag") : i18n("Delete %1 tags", leaves.size());
    }
    if (!deleteText.isEmpty()) {
        if (addSeparator) {
            popupMenu->addSeparator();
        }
        connect(popupMenu->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), deleteText), &QAction::triggered, this, [this, refNames] {
            deleteRefs(refNames);
        });
        addSeparator = true;
    }

    if (addSeparator) {
        popupMenu->addSeparator();
    }
    QStringList names;
    QStringList hashes;
    for (const ShadowTree *leaf : leaves) {
        names.append(Git::stripRefName(leaf->refName()));
        hashes.append(leaf->ref.hash);
    }
    connect(popupMenu->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy ref names")), &QAction::triggered, this, [names] {
        AppUtils::copyToClipboard(names.join(QLatin1Char('\n')));
    });
    // Not in TortoiseGit's ref menu, whose log list has it instead
    QAction *copyHash = popupMenu->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy SHA-1"));
    copyHash->setEnabled(!hashes.isEmpty());
    connect(copyHash, &QAction::triggered, this, [hashes] {
        AppUtils::copyToClipboard(hashes.join(QLatin1Char('\n')));
    });
    return popupMenu;
}

void BrowseRefsDlg::addSingleRefEntries(QMenu& popupMenu, const ShadowTree& leaf)
{
    // ShowContextMenu with one ref selected. Entries take the ref by name and value: a refresh finishing while the
    // menu is open rebuilds the tree
    const QString refName = leaf.refName();
    const bool isCommit = leaf.ref.objectType == GitObjectType::Commit;
    const bool isHead = leaf.isFrom(headsPrefix);
    const bool isRemote = leaf.isFrom(remotesPrefix);
    const bool showReflogOption = isHead || isRemote;
    const bool showRenameOption = isHead;
    const bool showEditBranchDescriptionOption = isHead && isCommit;
    const bool showCreateBranchOption = isRemote;
    // Only a ref under a known remote can be fetched
    const std::optional<RemoteBranch> remoteBranch = isRemote ? splitRemoteBranchName(refName, m_remotes) : std::nullopt;

    const auto addEntry = [this, &popupMenu](const QString& iconName, const QString& text, auto handler) {
        QAction *action = popupMenu.addAction(QIcon::fromTheme(iconName), text);
        connect(action, &QAction::triggered, this, handler);
        return action;
    };

    if (isCommit) {
        popupMenu.setDefaultAction(addEntry(QStringLiteral("view-history"), i18n("Show &log"), [this, refName] {
            showLog(refName);
        }));
    }
    if (showReflogOption) {
        addEntry(QStringLiteral("view-history"), i18n("Show &RefLog"), [this, refName] {
            showRefLog(refName);
        });
    }
    if (m_hasWC && isCommit) {
        popupMenu.addSeparator();
        addEntry(QStringLiteral("vcs-diff"), i18n("Compare with &working tree"), [this, refName] {
            compareWithWorkingTree(refName);
        });
    }
    popupMenu.addSeparator();

    if (remoteBranch) {
        const QString remote = remoteBranch->remote;
        addEntry(QStringLiteral("vcs-pull"), i18n("Fetch from \"%1\"", remote), [this, remote] {
            fetchFrom(remote);
        });
        popupMenu.addSeparator();
    }

    if (m_hasWC && isCommit) {
        const QString currentBranch = m_git.currentBranch();
        if (refName != headsPrefix + currentBranch) {
            addEntry(QStringLiteral("vcs-merge"), i18n("&Merge to \"%1\"...", currentBranch), [this, refName] {
                mergeRef(refName);
            });
        }
        addEntry(QStringLiteral("vcs-branch"), i18n("Sw&itch/Checkout to this..."), [this, refName] {
            switchToRef(refName);
        });
        popupMenu.addSeparator();
    }

    if (showCreateBranchOption) {
        const QString hash = leaf.ref.hash;
        addEntry(QStringLiteral("vcs-branch"), i18n("Create &Branch..."), [this, hash] {
            createBranch(hash);
        });
    }
    if (showEditBranchDescriptionOption) {
        const QString branch = leaf.refsHeadsName();
        addEntry(QStringLiteral("edit-rename"), i18n("Edit description"), [this, branch] {
            editBranchDescription(branch);
        });
    }
    if (showRenameOption) {
        addEntry(QStringLiteral("edit-rename"), i18n("Rename"), [this, refName] {
            startRename(refName);
        });
    }

    if (m_hasWC && isCommit && isHead) {
        popupMenu.addSeparator();
        const QString branch = leaf.refsHeadsName();
        if (!leaf.ref.upstreamRef.isEmpty()) {
            addEntry(QString(), i18n("Unset tracked branch"), [this, branch] {
                dropUpstream(branch);
            });
        }
        addEntry(QString(), i18n("Select tracked branch"), [this, branch] {
            setUpstream(branch);
        });
    }
}

void BrowseRefsDlg::addTwoRefEntries(QMenu& popupMenu, const VectorShadowTree& leaves)
{
    // ShowContextMenu with two refs selected
    const QString refName1 = leaves.at(0)->refName();
    const QString refName2 = leaves.at(1)->refName();
    const QString hash1 = leaves.at(0)->ref.hash;
    const QString hash2 = leaves.at(1)->ref.hash;
    connect(popupMenu.addAction(QIcon::fromTheme(QStringLiteral("vcs-diff")), i18n("Compare selected refs")),
            &QAction::triggered,
            this,
            [this, refName1, refName2] {
                compareRefs(refName1, refName2);
            });
    // eCmd_UnifiedDiff. Shift's alternative viewer doesn't apply: there is only the text editor
    connect(popupMenu.addAction(QIcon::fromTheme(QStringLiteral("vcs-diff")), i18n("Show changes as &unified diff")),
            &QAction::triggered,
            this,
            [this, hash1, hash2] {
                std::ignore = AppUtils::startShowUnifiedDiff(m_git, {.rev1 = hash1, .rev2 = hash2, .path = {}}, this);
            });
    const QStringList refNames{refName1, refName2};
    for (const QString& separator : {QStringLiteral(".."), QStringLiteral("...")}) {
        // eCmd_ViewLogRange and eCmd_ViewLogRangeReachableFromOnlyOne
        const QString range = twoSelectedRefs(refNames, m_lastSelected, separator);
        connect(popupMenu.addAction(QIcon::fromTheme(QStringLiteral("view-history")), i18n("Show log of %1", range)), &QAction::triggered, this, [this, range] {
            showLog(range);
        });
    }
}

QString BrowseRefsDlg::twoSelectedRefs(const QStringList& refNames, const QString& lastSelected, const QString& separator)
{
    Q_ASSERT(refNames.size() == 2);
    const QString& other = refNames.at(0) == lastSelected ? refNames.at(1) : refNames.at(0);
    return Git::stripRefName(other) + separator + Git::stripRefName(lastSelected);
}

void BrowseRefsDlg::showRefLog(const QString& refName)
{
    // eCmd_ShowReflog
    RefLogDlg refLogDlg(m_git.workingDirectory(), this);
    refLogDlg.setCurrentBranch(refName);
    refLogDlg.refresh();
    refLogDlg.exec();
}

void BrowseRefsDlg::compareWithWorkingTree(const QString& refName)
{
    // eCmd_DiffWC opens showcompare between the ref and the working tree, whose Changed Files dialog isn't ported;
    // as the log dialog's compares, git difftool --dir-diff shows every changed file in one window
    std::ignore = GitDiff::start(this, {.workingDirectory = m_git.workingDirectory(), .arguments = GitDiff::dirArguments({}, QString(), refName)});
}

void BrowseRefsDlg::compareRefs(const QString& refName1, const QString& refName2)
{
    // eCmd_Diff: CFileDiffDlg::SetDiff(nullptr, first, second) with the second as the base, which isn't ported; see
    // compareWithWorkingTree()
    std::ignore = GitDiff::start(this, {.workingDirectory = m_git.workingDirectory(), .arguments = GitDiff::dirArguments({}, refName1, refName2)});
}

void BrowseRefsDlg::fetchFrom(const QString& remote)
{
    // eCmd_Fetch
    std::ignore = FetchCommand::fetch(m_git.workingDirectory(), remote, this);
    refresh();
}

void BrowseRefsDlg::mergeRef(const QString& refName)
{
    // eCmd_Merge. TortoiseGit doesn't refresh; a merge moves the current branch, so the list is reloaded here
    if (MergeCommand::merge(m_git.workingDirectory(), refName, this)) {
        refresh();
    }
}

void BrowseRefsDlg::switchToRef(const QString& refName)
{
    // eCmd_Switch. Reloaded, as the switch may create a branch and changes which one is current (bold)
    if (AppUtils::switchTo(m_git.workingDirectory(), refName, this)) {
        refresh();
    }
}

void BrowseRefsDlg::createBranch(const QString& hash)
{
    // eCmd_CreateBranch
    CreateBranchTagParams params;
    params.initialRefName = hash;
    AppUtils::createBranchTag(m_git.workingDirectory(), params, this);
    refresh();
}

void BrowseRefsDlg::editBranchDescription(const QString& branch)
{
    // eCmd_EditBranchDescription: CInputDlg with the hint and title both "Edit description", starting from the
    // description the list shows
    const QString description = treeNode(headsPrefix + branch, nullptr, false).ref.description;
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this, i18n("Edit description"), i18n("Edit description"), description, &accepted);
    if (accepted) {
        AppUtils::updateBranchDescription(m_git, branch, text);
        refresh();
    }
}

void BrowseRefsDlg::startRename(const QString& refName)
{
    // eCmd_Rename: edit the name in place; onNameEdited() takes it from there
    for (int row = 0; row < m_listModel->rowCount(); ++row) {
        if (m_listModel->entry(row).tree->refName() == refName) {
            const QModelIndex index = m_listModel->index(row, RefLeafListModel::NameColumn);
            m_listRefLeafs->setCurrentIndex(index);
            m_listRefLeafs->edit(index);
            return;
        }
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): The parameters of RefLeafListModel::nameEdited
void BrowseRefsDlg::onNameEdited(const QString& refName, const QString& newText)
{
    // OnLvnEndlabeleditListRefLeafs
    if (!refName.startsWith(headsPrefix)) {
        KMessageBox::error(this, i18n("At the moment, you can only rename branches."), QString(), noNotify);
        return;
    }
    const QString origName = refName.mid(headsPrefix.size());
    QString newName;
    if (m_listCtrlRoot) {
        newName = m_listCtrlRoot->refName() + QLatin1Char('/');
    }
    newName += newText;
    if (!newName.startsWith(headsPrefix)) {
        KMessageBox::error(this, i18n("You cannot change the type of this ref with a rename."), QString(), noNotify);
        return;
    }
    const QString newNameTrunced = newName.mid(headsPrefix.size());
    try {
        std::ignore = m_git.run({QStringLiteral("branch"), QStringLiteral("-m"), origName, QStringLiteral("--"), newNameTrunced});
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, e.error().message, QString(), noNotify);
        return;
    }
    // TortoiseGit means to keep the tree node selected; the branch's new name may be under another one
    refresh(newName);
}

void BrowseRefsDlg::dropUpstream(const QString& branch)
{
    // eCmd_UpstreamDrop. As g_Git.UnsetConfigValue's result is ignored there, a key that isn't set doesn't matter
    for (const QString& key : {QStringLiteral("branch.%1.remote").arg(branch), QStringLiteral("branch.%1.merge").arg(branch)}) {
        try {
            m_git.unsetConfigValue(key);
        } catch (const RuntimeError& e) {
            if (e.error().category != ErrorCategory::Git) {
                throw;
            }
        }
    }
    refresh();
}

void BrowseRefsDlg::setUpstream(const QString& branch)
{
    // eCmd_UpstreamSet
    const QString newRef = pickRef(m_git.workingDirectory(), {.initialRef = QString(), .pickRefKind = PickRefRemote, .pickMultipleRefs = false}, this);
    if (newRef.isEmpty() || !newRef.startsWith(remotesPrefix)) {
        return;
    }
    const std::optional<RemoteBranch> split = splitRemoteBranchName(newRef, m_remotes);
    if (!split) {
        return;
    }
    // Setting the config keys directly might result in an invalid situation if the remote is not set to fetch the
    // desired upstream branch (in remote.x.fetch), cf. TortoiseGit issue #3638
    try {
        std::ignore = m_git.run(
            {QStringLiteral("branch"), QStringLiteral("--set-upstream-to=") + split->remote + QLatin1Char('/') + split->branch, QStringLiteral("--"), branch});
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this,
                           e.error().message + QStringLiteral("\n\n")
                               + i18n("This is usually caused when the setting \"remote.%1.fetch\" does not include the desired branch.", split->remote),
                           QString(),
                           noNotify);
        return;
    }
    refresh();
}

void BrowseRefsDlg::onListContextMenu(const QPoint& pos)
{
    const std::unique_ptr<QMenu> popupMenu = createListContextMenu();
    popupMenu->exec(m_listRefLeafs->viewport()->mapToGlobal(pos));
}

void BrowseRefsDlg::onTreeContextMenu(const QPoint& pos)
{
    // OnContextMenu_RefTreeCtrl: the node under the cursor is selected first; empty space leaves the selection
    // alone and offers only Copy ref names
    const ShadowTree *tree = nullptr;
    const QModelIndex index = m_refTree->indexAt(pos);
    if (index.isValid()) {
        m_refTree->setCurrentIndex(index);
        tree = selectedTreeNode();
    }
    const std::unique_ptr<QMenu> popupMenu = createTreeContextMenu(tree);
    popupMenu->exec(m_refTree->viewport()->mapToGlobal(pos));
}

std::unique_ptr<QMenu> BrowseRefsDlg::createTreeContextMenu()
{
    return createTreeContextMenu(selectedTreeNode());
}

std::unique_ptr<QMenu> BrowseRefsDlg::createTreeContextMenu(const ShadowTree *tree)
{
    // ShowContextMenu with a tree node and no refs selected. Entries take names by value: a refresh finishing while
    // the menu is open rebuilds the tree
    auto popupMenu = std::make_unique<QMenu>(this);
    const auto addEntry = [this, &popupMenu](const QString& iconName, const QString& text, auto handler) {
        connect(popupMenu->addAction(QIcon::fromTheme(iconName), text), &QAction::triggered, this, handler);
    };
    if (tree && tree->isFrom("refs/remotes"_L1)) {
        addEntry(QStringLiteral("configure"), i18n("Manage Remotes"), [this] {
            manageRemotes();
        });
        // Only on a node under a known remote
        if (const std::optional<RemoteBranch> split = splitRemoteBranchName(tree->refName(), m_remotes)) {
            const QString remote = split->remote;
            addEntry(QStringLiteral("vcs-pull"), i18n("Fetch from \"%1\"", remote), [this, remote] {
                fetchFrom(remote);
            });
            addEntry(QStringLiteral("edit-delete"), i18n("Delete remote tags..."), [this, remote] {
                deleteRemoteTags(remote);
            });
        }
    }
    if (tree && tree->isFrom("refs/heads"_L1)) {
        addEntry(QStringLiteral("vcs-branch"), i18n("Create &Branch..."), [this] {
            // eCmd_CreateBranch with no ref selected: based on HEAD
            createBranch(QString());
        });
    }
    if (tree && tree->isFrom("refs/tags"_L1)) {
        addTreeTagEntries(*popupMenu);
    }
    // TortoiseGit adds no separator after the namespace's entries here. With no refs selected there is nothing to
    // copy, as there
    addEntry(QStringLiteral("edit-copy"), i18n("Copy ref names"), [] {
        AppUtils::copyToClipboard(QString());
    });
    return popupMenu;
}

void BrowseRefsDlg::addTreeTagEntries(QMenu& popupMenu)
{
    // ShowContextMenu on refs/tags
    connect(popupMenu.addAction(QIcon::fromTheme(QStringLiteral("tag")), i18n("Create &Tag...")), &QAction::triggered, this, &BrowseRefsDlg::createTag);
    connect(popupMenu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Delete all tags")),
            &QAction::triggered,
            this,
            &BrowseRefsDlg::deleteAllTags);
    if (m_remotes.isEmpty()) {
        return;
    }
    popupMenu.addSeparator();
    for (const QString& remote : std::as_const(m_remotes)) {
        connect(popupMenu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Delete remote tags on \"%1\"...", remote)),
                &QAction::triggered,
                this,
                [this, remote] {
                    deleteRemoteTags(remote);
                });
    }
}

void BrowseRefsDlg::manageRemotes()
{
    // eCmd_ManageRemotes
    AppUtils::launchRemoteSetting(m_git.workingDirectory(), this);
    refresh();
}

void BrowseRefsDlg::createTag()
{
    // eCmd_CreateTag
    CreateBranchTagParams params;
    params.isTag = true;
    AppUtils::createBranchTag(m_git.workingDirectory(), params, this);
    refresh();
}

void BrowseRefsDlg::deleteAllTags()
{
    // eCmd_DeleteAllTags: selects every ref in the list, then deletes them as Delete tags does
    m_listRefLeafs->selectAll();
    QStringList refNames;
    for (int row = 0; row < m_listModel->rowCount(); ++row) {
        refNames.append(m_listModel->entry(row).tree->refName());
    }
    // TortoiseGit asks even with no tags; there is nothing to ask about
    if (!refNames.isEmpty() && confirmDeleteRef(refNames)) {
        std::ignore = doDeleteRefs(refNames);
    }
    refresh();
}

void BrowseRefsDlg::deleteRemoteTags(const QString& remote)
{
    // eCmd_DeleteRemoteTag. TortoiseGit doesn't refresh; it costs nothing and remote tags may have been fetched
    DeleteRemoteTagDlg deleteRemoteTagDlg(m_git.workingDirectory(), remote, this);
    deleteRemoteTagDlg.exec();
    refresh();
}

void BrowseRefsDlg::onListDoubleClicked()
{
    // OnNMDblclkListRefLeafs
    const VectorShadowTree leaves = selectedLeaves();
    if (leaves.isEmpty()) {
        return;
    }
    if (m_wantPick) {
        accept();
        return;
    }
    // TODO: TortoiseGit opens the repository browser for a ref to a tree
    showLog(leaves.constFirst()->refName());
}

QStringList BrowseRefsDlg::showLogArguments(const QString& refName) const
{
    return {QStringLiteral("--command"),
            QStringLiteral("log"),
            QStringLiteral("--path"),
            m_git.workingDirectory(),
            QStringLiteral("--range"),
            m_git.fixBranchName(refName)};
}

void BrowseRefsDlg::showLog(const QString& refName) const
{
    std::ignore = AppUtils::runTortoiseGitProc(showLogArguments(refName));
}

void BrowseRefsDlg::deleteRefs(const QStringList& refNames)
{
    // eCmd_DeleteBranch, eCmd_DeleteRemoteBranch and eCmd_DeleteTag
    if (confirmDeleteRef(refNames)) {
        std::ignore = doDeleteRefs(refNames);
    }
    refresh();
}

bool BrowseRefsDlg::confirmDeleteRef(const QStringList& refNames)
{
    Q_ASSERT(!refNames.isEmpty());
    const QString& first = refNames.constFirst();
    const bool isRemoteBranch = first.startsWith(remotesPrefix);
    const bool isBranch = isRemoteBranch || first.startsWith(headsPrefix);
    QString message;
    bool warning = false;
    if (isBranch && refNames.size() == 1) {
        const QString branchToDelete = first.mid(isRemoteBranch ? remotesPrefix.size() : headsPrefix.size());
        message = i18n("Do you really want to delete \"%1\"?", branchToDelete);
        // Check if the branch is fully merged into HEAD
        if (!m_git.isFastForward(first, QStringLiteral("HEAD"))) {
            message += QStringLiteral("\n\n") + i18n("This branch is not fully merged into HEAD.");
            warning = true;
        }
    } else if (isBranch) {
        message = i18n("Do you really want to permanently delete the %1 selected refs? It can NOT be recovered!", refNames.size());
        message += QStringLiteral("\n\n") + i18n("It has not been checked if these branches have been fully merged into HEAD.");
        warning = true;
    } else if (first.startsWith(tagsPrefix)) {
        message = refNames.size() == 1 ? i18n("Do you really want to delete \"%1\"?", first.mid(tagsPrefix.size()))
                                       : i18n("Do you really want to permanently delete the %1 selected refs? It can NOT be recovered!", refNames.size());
    }
    if (isRemoteBranch) {
        message += QStringLiteral("\n\n") + i18n("This action will remove the branches on the remote.");
        warning = true;
    }
    return askYesNo(this, message, warning);
}

bool BrowseRefsDlg::doDeleteRefs(const QStringList& refNames)
{
    // Remote branches are deleted in one push per remote: faster, and fewer password prompts
    if (areAllFrom(refNames, remotesPrefix)) {
        std::map<QString, QStringList> remoteBranches;
        for (const QString& refName : refNames) {
            if (const std::optional<RemoteBranch> split = splitRemoteBranchName(refName, m_remotes)) {
                remoteBranches[split->remote].append(split->branch);
            }
        }
        for (const auto& [remote, branches] : remoteBranches) {
            if (!deleteRemoteRefs(remote, branches)) {
                return false;
            }
        }
        return true;
    }
    return std::ranges::all_of(refNames, [this](const QString& refName) {
        return doDeleteRef(refName);
    });
}

bool BrowseRefsDlg::doDeleteRef(const QString& completeRefName)
{
    if (completeRefName.startsWith(remotesPrefix)) {
        const std::optional<RemoteBranch> split = splitRemoteBranchName(completeRefName, m_remotes);
        return split && deleteRemoteRefs(split->remote, {split->branch});
    }
    if (!completeRefName.startsWith(headsPrefix) && !completeRefName.startsWith(tagsPrefix)) {
        return true;
    }
    try {
        m_git.deleteRef(completeRefName);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        KMessageBox::error(this, i18n("Could not delete reference.") + QStringLiteral("\n") + e.error().message, QString(), noNotify);
        return false;
    }
    return true;
}

bool BrowseRefsDlg::deleteRemoteRefs(const QString& remote, const QStringList& branches)
{
    // TortoiseGit runs this behind a "Deleting remote refs..." progress window; ProgressDlg shows git's output too
    ProgressDlg progress(m_git.workingDirectory(), this);
    QStringList refs;
    for (const QString& branch : branches) {
        refs.append(headsPrefix + branch);
    }
    progress.addGitCmd(Git::deleteRemoteRefsArguments(remote, refs));
    progress.exec();
    return progress.gitStatus() == 0;
}

std::optional<RemoteBranch> BrowseRefsDlg::splitRemoteBranchName(QString ref, const QStringList& remotes)
{
    if (ref.startsWith(remotesPrefix)) {
        ref = ref.mid(remotesPrefix.size());
    } else if (ref.startsWith("remotes/"_L1)) {
        ref = ref.mid(QLatin1StringView("remotes/").size());
    }
    for (const QString& candidate : remotes) {
        if (ref.startsWith(candidate + QLatin1Char('/'))) {
            return RemoteBranch{.remote = candidate, .branch = ref.mid(candidate.size() + 1)};
        }
        if (ref == candidate) {
            return RemoteBranch{.remote = candidate, .branch = {}};
        }
    }
    return std::nullopt;
}

QString BrowseRefsDlg::fullRefName(const QString& partialRefName) const
{
    const ShadowTree *leaf = m_treeRoot.findLeaf(partialRefName);
    return leaf ? leaf->refName() : QString();
}

void BrowseRefsDlg::onCurrentBranchClicked()
{
    // TortoiseGit takes CGit::GetCurrentBranch(true), the hash when detached; HEAD names the same commit
    m_pickedRef = m_currentBranch.isEmpty() ? QStringLiteral("HEAD") : m_currentBranch;
    m_pickedRefSet = true;
    accept();
}

void BrowseRefsDlg::done(int result)
{
    if (!m_pickedRefSet) {
        m_pickedRef = selectedRef(true, false);
    }
    QDialog::done(result);
}

} // namespace kgt
