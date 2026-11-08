#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Regenerates the shipped icons in src/proc/icons/ from TortoiseGit's originals in src/proc/icons/original/,
# recoloured to pastel shades that read on both a light (Breeze, #ffffff) and a dark (Breeze Dark, #202326)
# background. Shapes are untouched; only colour changes.
#   scripts/recolour-icons.sh                  # Rewrite src/proc/icons/*.png
#   scripts/recolour-icons.sh --sheets <dir>   # Also write before/after contact sheets (16 and 32 px) into <dir>
#
# Each pixel is converted to OKLCh. Colourful pixels (chroma above about 0.04) get their lightness clamped into
# [lmin, lmax] and their chroma capped at cmax, so dark blues are lifted, and harsh reds and yellows are softened.
# Greys and whites (the document pages) are left alone: they already read on both backgrounds. A ramp between
# chroma 0.04 and 0.08 blends the two, so antialiased edges don't band. Alpha is kept as is.
# Needs ImageMagick 7 (magick) built with HDRI, for the unclamped OKLCh channels.
set -euo pipefail

lmin=0.66
lmax=0.80
cmax=0.13

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
icons="$repo/src/proc/icons"
originals="$icons/original"

# Writes $2, the recoloured copy of $1
recolour() {
    local weight='wt=min(1,max(0,(g-0.04)/0.04));'
    magick "$1" \( +clone -alpha extract \) \
        \( -clone 0 -alpha off -colorspace OkLCH \
            -channel R -fx "$weight r*(1-wt)+min(max(r,$lmin),$lmax)*wt" \
            -channel G -fx "$weight g*(1-wt)+min(g,$cmax)*wt" \
            +channel -colorspace sRGB \) \
        -delete 0 +swap -alpha off -compose CopyOpacity -composite \
        -define png:exclude-chunks=date,time -strip "$2"
}

# Writes $3, a contact sheet of the icons in $1 at 16 and 32 px, on white and on #202326 side by side; $2 is a
# scratch directory
contact_sheet() {
    local src="$1" tmp="$2" out="$3" size bg
    for size in 16 32; do
        for bg in ffffff 202326; do
            magick montage "$src"/action*-"$size".png -background "#$bg" -geometry +8+8 -tile 7x1 \
                "$tmp/row-$size-$bg.png"
        done
        magick "$tmp/row-$size-ffffff.png" "$tmp/row-$size-202326.png" +append "$tmp/pair-$size.png"
    done
    magick "$tmp/pair-16.png" "$tmp/pair-32.png" -background '#808080' -gravity west -append -scale 200% "$out"
}

main() {
    local sheets=""
    if [[ "${1:-}" == "--sheets" ]]; then
        sheets="${2:?--sheets needs a directory}"
    fi

    local file
    for file in "$originals"/*.png; do
        recolour "$file" "$icons/$(basename "$file")"
    done

    if [[ -n "$sheets" ]]; then
        mkdir -p "$sheets"
        local tmp
        tmp="$(mktemp -d)"
        contact_sheet "$originals" "$tmp" "$sheets/icons-before.png"
        contact_sheet "$icons" "$tmp" "$sheets/icons-after.png"
        rm -rf "$tmp"
    fi
}

main "$@"
