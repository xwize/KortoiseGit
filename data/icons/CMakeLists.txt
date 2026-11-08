# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

# The PNGs are built by make-icon.sh and committed, so building KortoiseGit does not need photocraft-cli
ecm_install_icons(
    ICONS
        16-apps-kortoisegit.png
        22-apps-kortoisegit.png
        32-apps-kortoisegit.png
        48-apps-kortoisegit.png
        64-apps-kortoisegit.png
        128-apps-kortoisegit.png
        256-apps-kortoisegit.png
    DESTINATION ${KDE_INSTALL_ICONDIR}
)
