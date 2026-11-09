// SPDX-FileCopyrightText: 2003-2008 TortoiseSVN
// SPDX-FileCopyrightText: 2009-2021, 2023-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/AboutDlg.cpp and its IDD_ABOUT resource.
 */

#include "aboutdlg.hpp"

#include "apputils.hpp"
#include "git/git.hpp"
#include "utils/runtimeerror.hpp"
#include "utils/version.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextDocument>
#include <QVBoxLayout>

namespace
{

using namespace kgt;

constexpr auto websiteUrl = "https://github.com/xwize/KortoiseGit";
constexpr int logoSize = 64;
constexpr int logoTextScale = 2;
// About the width of IDD_ABOUT's version box
constexpr int versionColumns = 72;
// IDS_ABOUTVERSION's two lines and the empty one after them, and room for a long application directory to wrap
constexpr int versionLines = 5;

/**
 * Ported from CAboutDlg::OnInitDialog.
 * @returns The output of git --version, or why it couldn't be run.
 */
QString gitVersion()
{
    try {
        // It needs no repository, so any directory that is sure to exist will do
        return QString::fromUtf8(Git(QDir::tempPath()).run({QStringLiteral("--version")}, RunMode::ReadOnly)).trimmed();
    } catch (const RuntimeError& e) {
        // Every category means the same here: the reason is shown in the box
        return i18n("Git not found (%1)", e.error().message).trimmed();
    }
}

/**
 * Ported from CAboutDlg::OnInitDialog.
 * @returns The Version Information box's text, following IDS_ABOUTVERSION.
 */
QString versionText()
{
    return i18n("KortoiseGit %1 (%2)\n%3\n", versionString(), QCoreApplication::applicationDirPath(), gitVersion());
}

/**
 * @param text The label's text, plain because an address in angle brackets would otherwise be taken for a tag.
 * @param parent The dialog.
 * @returns A label that wraps.
 */
QLabel *wrappedLabel(const QString& text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

/**
 * Stands in for TortoiseGit's IDR_TGITLOGO, shown still: the app icon and the product's name.
 * @param parent The dialog.
 * @returns The logo's row.
 */
QLayout *createLogo(QWidget *parent)
{
    auto *row = new QHBoxLayout;
    auto *icon = new QLabel(parent);
    icon->setPixmap(AppUtils::appIcon().pixmap(logoSize, logoSize));
    row->addWidget(icon);
    auto *name = new QLabel(i18n("KortoiseGit"), parent);
    name->setTextFormat(Qt::PlainText);
    QFont font = name->font();
    font.setPointSizeF(font.pointSizeF() * logoTextScale);
    font.setBold(true);
    name->setFont(font);
    row->addWidget(name, 1);
    return row;
}

/**
 * @param parent The dialog.
 * @returns IDC_VERSIONBOX, holding IDC_VERSIONABOUT: a read-only edit without a border, so the text can be selected.
 */
QGroupBox *createVersionBox(QWidget *parent)
{
    auto *box = new QGroupBox(i18n("Version Information"), parent);
    auto *layout = new QVBoxLayout(box);
    auto *version = new QPlainTextEdit(versionText(), box);
    version->setReadOnly(true);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    version->setFrameShape(QFrame::NoFrame);
    version->viewport()->setAutoFillBackground(false);
    const QFontMetrics metrics = version->fontMetrics();
    version->setFixedHeight(metrics.lineSpacing() * versionLines + 2 * static_cast<int>(version->document()->documentMargin()));
    version->setMinimumWidth(metrics.averageCharWidth() * versionColumns);
    layout->addWidget(version);
    return box;
}

} // namespace

namespace kgt
{

AboutDlg::AboutDlg(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("About KortoiseGit"));
    auto *layout = new QVBoxLayout(this);
    // IDD_ABOUT has no resizing frame
    layout->setSizeConstraint(QLayout::SetFixedSize);

    layout->addLayout(createLogo(this));
    const QString basedOn = i18n(
        "Based on TortoiseGit (https://tortoisegit.org/). "
        "KortoiseGit is not affiliated with the TortoiseGit project.");
    layout->addWidget(wrappedLabel(basedOn, this));
    layout->addSpacing(layout->spacing());
    layout->addWidget(wrappedLabel(i18n("Significant contributions by: (see Git repository for a complete list)"), this));
    layout->addWidget(wrappedLabel(QStringLiteral("James Jones <this.verb@gmail.com>"), this));
    layout->addSpacing(layout->spacing());

    auto *webLink = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>").arg(QLatin1String(websiteUrl), i18n("Visit our website").toHtmlEscaped()), this);
    webLink->setTextFormat(Qt::RichText);
    webLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    webLink->setOpenExternalLinks(true);
    webLink->setToolTip(QLatin1String(websiteUrl));
    layout->addWidget(webLink);

    layout->addWidget(createVersionBox(this));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
    // CAboutDlg::OnInitDialog: OK has the focus, not the first control that could take it
    buttons->button(QDialogButtonBox::Ok)->setFocus();
}

} // namespace kgt
