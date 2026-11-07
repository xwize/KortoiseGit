// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// The plugin factory Dolphin loads. Everything else is in the kgtplugin library, so the tests can link it.

#include "shellext.hpp"

#include <KPluginFactory>

// NOLINTNEXTLINE(readability-identifier-naming) KPluginFactory names its factory class after the plugin target
K_PLUGIN_CLASS_WITH_JSON(kgt::ShellExt, "kortoisegitplugin.json")

#include "kortoisegitplugin.moc"
