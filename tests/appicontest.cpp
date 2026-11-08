// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "apputils.hpp"

#include <QDir>
#include <QFile>
#include <QIcon>
#include <QTemporaryDir>
#include <QTest>

using kgt::AppUtils;

class AppIconTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_themes.isValid());
        QIcon::setThemeSearchPaths({m_themes.path()});
        QIcon::setThemeName(QStringLiteral("hicolor"));
    }

    void initIconResourcesRegistersTheFile()
    {
        AppUtils::initIconResources();
        QVERIFY(QFile::exists(QStringLiteral(":/kortoisegit/icons/kortoisegit-16.png")));
    }

    // Running from build/bin: no theme has the icon, so the copy in icons.qrc is used
    void fallsBackToBundledIcon()
    {
        const QIcon icon = AppUtils::appIcon();
        QVERIFY(!icon.isNull());
        QVERIFY(icon.name().isEmpty());
        const QList<QSize> sizes = icon.availableSizes();
        QVERIFY(sizes.contains(QSize(16, 16)));
        QVERIFY(sizes.contains(QSize(256, 256)));
    }

    // Installed: ecm_install_icons put it in the hicolor theme, which wins over the bundled copy
    void prefersInstalledThemeIcon()
    {
        const QDir hicolor(m_themes.filePath(QStringLiteral("hicolor")));
        QVERIFY(hicolor.mkpath(QStringLiteral("16x16/apps")));
        QFile index(hicolor.filePath(QStringLiteral("index.theme")));
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write("[Icon Theme]\nName=Hicolor\nDirectories=16x16/apps\n\n[16x16/apps]\nSize=16\nContext=Applications\nType=Threshold\n");
        index.close();
        QVERIFY(QFile::copy(QStringLiteral(":/kortoisegit/icons/kortoisegit-16.png"), hicolor.filePath(QStringLiteral("16x16/apps/kortoisegit.png"))));
        QIcon::setThemeSearchPaths({m_themes.path()}); // Drops Qt's cached scan of the theme

        QCOMPARE(AppUtils::appIcon().name(), QStringLiteral("kortoisegit"));
    }

private:
    QTemporaryDir m_themes;
};

QTEST_MAIN(AppIconTest)

#include "appicontest.moc"
