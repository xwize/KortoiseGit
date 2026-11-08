// SPDX-FileCopyrightText: 2009-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/BrowseRefsDlg.cpp and its IDD_BROWSE_REFS resource

#include "browserefsdlg.hpp"

#include "apputils.hpp"
#include "browserefsdlgfilter.hpp"
#include "progressdlg.hpp"

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
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
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
bool areAllFrom(const QList<const ShadowTree *>& leaves, QLatin1StringView from)
{
    return std::ranges::all_of(leaves, [from](const ShadowTree *leaf) {
        return leaf->isFrom(from);
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

/**
 * @param text What to put on the clipboard.
 */
void copyToClipboard(const QString& text)
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

} // namespace

namespace kgt
{

BrowseRefsDlg::BrowseRefsDlg(const QString& workingTree, QWidget *parent)
    : QDialog(parent)
    , m_git(workingTree)
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
    connect(m_listRefLeafs->selectionModel(), &QItemSelectionModel::selectionChanged, this, &BrowseRefsDlg::updateInfoLabel);
    connect(m_listRefLeafs, &QWidget::customContextMenuRequested, this, &BrowseRefsDlg::onListContextMenu);
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
    bool addSeparator = false;
    if (leaves.size() == 1) {
        if (m_wantPick) {
            connect(popupMenu->addAction(i18n("Select")), &QAction::triggered, this, &QDialog::accept);
            popupMenu->addSeparator();
        }
        if (leaves.constFirst()->ref.objectType == GitObjectType::Commit) {
            QAction *log = popupMenu->addAction(QIcon::fromTheme(QStringLiteral("view-history")), i18n("Show &log"));
            const QString refName = leaves.constFirst()->refName();
            connect(log, &QAction::triggered, this, [this, refName] {
                showLog(refName);
            });
            popupMenu->setDefaultAction(log);
        }
        // TODO: Browse repository, reflog, compare with working tree, fetch, merge, switch, create branch, edit
        // description, rename and the upstream entries come with Phase 4
        addSeparator = true;
    }
    // TODO: with two refs selected, the compare and log range entries come with Phase 4

    QString deleteText;
    if (!leaves.isEmpty() && areAllFrom(leaves, remotesPrefix)) {
        deleteText = leaves.size() == 1 ? i18n("Delete remote branch") : i18n("Delete %1 remote branches", leaves.size());
    } else if (!leaves.isEmpty() && areAllFrom(leaves, headsPrefix)) {
        deleteText = leaves.size() == 1 ? i18n("Delete branch") : i18n("Delete %1 branches", leaves.size());
    } else if (!leaves.isEmpty() && areAllFrom(leaves, tagsPrefix)) {
        deleteText = leaves.size() == 1 ? i18n("Delete tag") : i18n("Delete %1 tags", leaves.size());
    }
    if (!deleteText.isEmpty()) {
        if (addSeparator) {
            popupMenu->addSeparator();
        }
        connect(popupMenu->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), deleteText), &QAction::triggered, this, [this, leaves] {
            deleteRefs(leaves);
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
        copyToClipboard(names.join(QLatin1Char('\n')));
    });
    // Not in TortoiseGit's ref menu, whose log list has it instead
    QAction *copyHash = popupMenu->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy SHA-1"));
    copyHash->setEnabled(!hashes.isEmpty());
    connect(copyHash, &QAction::triggered, this, [hashes] {
        copyToClipboard(hashes.join(QLatin1Char('\n')));
    });
    return popupMenu;
}

void BrowseRefsDlg::onListContextMenu(const QPoint& pos)
{
    const std::unique_ptr<QMenu> popupMenu = createListContextMenu();
    popupMenu->exec(m_listRefLeafs->viewport()->mapToGlobal(pos));
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

void BrowseRefsDlg::showLog(const QString& refName)
{
    // TODO: pass --range refName (FixBranchName) once LogCommand takes a revision; for now the whole history
    Q_UNUSED(refName)
    std::ignore = AppUtils::runTortoiseGitProc({QStringLiteral("--command"), QStringLiteral("log"), QStringLiteral("--path"), m_git.workingDirectory()});
}

void BrowseRefsDlg::deleteRefs(const VectorShadowTree& leaves)
{
    // eCmd_DeleteBranch, eCmd_DeleteRemoteBranch and eCmd_DeleteTag
    if (confirmDeleteRef(leaves)) {
        std::ignore = doDeleteRefs(leaves);
    }
    refresh();
}

bool BrowseRefsDlg::confirmDeleteRef(const VectorShadowTree& leaves)
{
    Q_ASSERT(!leaves.isEmpty());
    const ShadowTree *first = leaves.constFirst();
    const bool isRemoteBranch = first->isFrom(remotesPrefix);
    const bool isBranch = isRemoteBranch || first->isFrom(headsPrefix);
    QString message;
    bool warning = false;
    if (isBranch && leaves.size() == 1) {
        const QString branchToDelete = first->refName().mid(isRemoteBranch ? remotesPrefix.size() : headsPrefix.size());
        message = i18n("Do you really want to delete \"%1\"?", branchToDelete);
        // Check if the branch is fully merged into HEAD
        if (!isFastForward(first->refName(), QStringLiteral("HEAD"))) {
            message += QStringLiteral("\n\n") + i18n("This branch is not fully merged into HEAD.");
            warning = true;
        }
    } else if (isBranch) {
        message = i18n("Do you really want to permanently delete the %1 selected refs? It can NOT be recovered!", leaves.size());
        message += QStringLiteral("\n\n") + i18n("It has not been checked if these branches have been fully merged into HEAD.");
        warning = true;
    } else if (first->isFrom(tagsPrefix)) {
        message = leaves.size() == 1 ? i18n("Do you really want to delete \"%1\"?", first->refName().mid(tagsPrefix.size()))
                                     : i18n("Do you really want to permanently delete the %1 selected refs? It can NOT be recovered!", leaves.size());
    }
    if (isRemoteBranch) {
        message += QStringLiteral("\n\n") + i18n("This action will remove the branches on the remote.");
        warning = true;
    }
    return askYesNo(this, message, warning);
}

bool BrowseRefsDlg::doDeleteRefs(const VectorShadowTree& leaves)
{
    // Remote branches are deleted in one push per remote: faster, and fewer password prompts
    if (areAllFrom(leaves, remotesPrefix)) {
        std::map<QString, QStringList> remoteBranches;
        for (const ShadowTree *leaf : leaves) {
            if (const std::optional<RemoteBranch> split = splitRemoteBranchName(leaf->refName(), m_remotes)) {
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
    return std::ranges::all_of(leaves, [this](const ShadowTree *leaf) {
        return doDeleteRef(leaf->refName());
    });
}

bool BrowseRefsDlg::doDeleteRef(const QString& completeRefName)
{
    if (completeRefName.startsWith(remotesPrefix)) {
        const std::optional<RemoteBranch> split = splitRemoteBranchName(completeRefName, m_remotes);
        return split && deleteRemoteRefs(split->remote, {split->branch});
    }
    // CGit::DeleteRef
    QStringList arguments;
    if (completeRefName.startsWith(headsPrefix)) {
        arguments = {QStringLiteral("branch"), QStringLiteral("-D"), QStringLiteral("--"), completeRefName.mid(headsPrefix.size())};
    } else if (completeRefName.startsWith(tagsPrefix)) {
        arguments = {QStringLiteral("tag"), QStringLiteral("-d"), QStringLiteral("--"), completeRefName.mid(tagsPrefix.size())};
    } else {
        return true;
    }
    try {
        std::ignore = m_git.run(arguments);
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
    progress.addGitCmd(deleteRemoteRefsArguments(remote, branches));
    progress.exec();
    return progress.gitStatus() == 0;
}

QStringList BrowseRefsDlg::deleteRemoteRefsArguments(const QString& remote, const QStringList& branches)
{
    QStringList arguments{QStringLiteral("push"), QStringLiteral("--progress"), QStringLiteral("--"), remote};
    for (const QString& branch : branches) {
        arguments.append(QStringLiteral(":refs/heads/") + branch);
    }
    return arguments;
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

bool BrowseRefsDlg::isFastForward(const QString& from, const QString& target) const
{
    // CGit::IsFastForward: from is an ancestor of to. Any failure counts as not merged, as in TortoiseGit
    try {
        std::ignore = m_git.run({QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), from, target}, RunMode::ReadOnly);
        return true;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return false;
    }
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
