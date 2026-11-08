#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds KortoiseGit's app icon with photocraft-cli: the layered kortoisegit.pcraft source (and hand-tuned
# kortoisegit-16/-22/-32.pcraft for the sizes where downscaling blurs the plates) and the hicolor PNGs beside this script.
# The colours follow the GUIDELINES.md icon rule: pastel, readable on #ffffff and on #202326, so the outlines are
# mid-tone green rather than dark.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:-$here}"

outline='#69a275'
rim='#f2e8c3'
dome_lit='#aee2a3'
dome='#8bd28d'
dome_dim='#6fc082'
plate='#e8be62'
plate_lit='#eed592'

cmds=()
# Queues one engine command for the next `build`
add() { cmds+=(--cmd "$1" --params "$2"); }
# shape <name> <extra shape.create JSON members>
shape() { add shape.create "{\"name\":\"$1\",$2}"; }
# stroke_only <width>: the members of a shape that is only an outline, with round ends
stroke_only() { printf '"fill":null,"stroke":{"width":%s,"color":"%s","align":"center","cap":"round","join":"round"}' "$1" "$outline"; }

# build <size> <base name>: runs the queued commands on a new transparent document and saves it as a layered source
build() {
    photocraft-cli run --new "{\"width\":$1,\"height\":$1,\"background\":\"transparent\",\"name\":\"kortoisegit\"}" \
        "${cmds[@]}" --out "$out/$2.pcraft" >/dev/null
    cmds=()
}

# The full design, drawn on a 1024 px canvas: a shell seen from above, a pale rim with its marginal plates, and a domed
# back of six plates around a central hexagon carrying a small git branch.
make_full() {
    local name="$1" with_branch="$2"
    # Twelve scalloped lobes, one per marginal plate
    local edge='"kind":"path","path":{"subpaths":[{"closed":true,"knots":[{"anchor":[916,512],"in":[970.1,497.4],"out":[970.1,526.6],"smooth":false},{"anchor":[868.2,717.7],"in":[922.3,732.3],"out":[907.6,757.5],"smooth":false},{"anchor":[725.6,882],"in":[765,921.8],"out":[740,936.2],"smooth":false},{"anchor":[512,948],"in":[526.3,1002.1],"out":[497.7,1002.1],"smooth":false},{"anchor":[298.4,882],"in":[284,936.2],"out":[259,921.8],"smooth":false},{"anchor":[155.8,717.7],"in":[116.4,757.5],"out":[101.7,732.3],"smooth":false},{"anchor":[108,512],"in":[53.9,526.6],"out":[53.9,497.4],"smooth":false},{"anchor":[155.8,306.3],"in":[101.7,291.7],"out":[116.4,266.5],"smooth":false},{"anchor":[298.4,142],"in":[259,102.2],"out":[284,87.8],"smooth":false},{"anchor":[512,76],"in":[497.7,21.9],"out":[526.3,21.9],"smooth":false},{"anchor":[725.6,142],"in":[740,87.8],"out":[765,102.2],"smooth":false},{"anchor":[868.2,306.3],"in":[907.6,266.5],"out":[922.3,291.7],"smooth":false}]}]}'
    shape Rim "$edge,\"fill\":\"$rim\",\"stroke\":null"
    shape 'Rim plates' "$(stroke_only 26),\"kind\":\"path\",\"path\":{\"subpaths\":[
        {\"closed\":false,\"knots\":[[832,512],[916,512]]},
        {\"closed\":false,\"knots\":[[795.3,675.6],[868.2,717.7]]},
        {\"closed\":false,\"knots\":[[683.6,809.1],[725.6,882]]},
        {\"closed\":false,\"knots\":[[512,864],[512,948]]},
        {\"closed\":false,\"knots\":[[340.4,809.1],[298.4,882]]},
        {\"closed\":false,\"knots\":[[228.7,675.6],[155.8,717.7]]},
        {\"closed\":false,\"knots\":[[192,512],[108,512]]},
        {\"closed\":false,\"knots\":[[228.7,348.4],[155.8,306.3]]},
        {\"closed\":false,\"knots\":[[340.4,214.9],[298.4,142]]},
        {\"closed\":false,\"knots\":[[512,160],[512,76]]},
        {\"closed\":false,\"knots\":[[683.6,214.9],[725.6,142]]},
        {\"closed\":false,\"knots\":[[795.3,348.4],[868.2,306.3]]}]}"
    shape Outline "$(stroke_only 44),$edge"
    shape Dome "\"kind\":\"ellipse\",\"rect\":[192,160,640,704],
        \"fill\":{\"gradient\":{\"style\":\"linear\",\"angle\":-90,\"stops\":[[0,\"$dome_lit\"],[0.55,\"$dome\"],[1,\"$dome_dim\"]]}},
        \"stroke\":{\"width\":36,\"color\":\"$outline\",\"align\":\"center\"}"
    shape Seams "$(stroke_only 32),\"kind\":\"path\",\"path\":{\"subpaths\":[
        {\"closed\":false,\"knots\":[[512,312],[512,160]]},
        {\"closed\":false,\"knots\":[[685.2,412],[795.3,348.4]]},
        {\"closed\":false,\"knots\":[[685.2,612],[795.3,675.6]]},
        {\"closed\":false,\"knots\":[[512,712],[512,864]]},
        {\"closed\":false,\"knots\":[[338.8,612],[228.7,675.6]]},
        {\"closed\":false,\"knots\":[[338.8,412],[228.7,348.4]]}]}"
    shape 'Centre plate' "\"kind\":\"path\",\"path\":{\"subpaths\":[{\"closed\":true,
            \"knots\":[[512,312],[685.2,412],[685.2,612],[512,712],[338.8,612],[338.8,412]]}]},
        \"fill\":{\"gradient\":{\"style\":\"linear\",\"angle\":-90,\"stops\":[[0,\"$plate_lit\"],[1,\"$plate\"]]}},
        \"stroke\":{\"width\":36,\"color\":\"$outline\",\"align\":\"center\",\"join\":\"round\"}"
    if [[ $with_branch == branch ]]; then
        shape 'Branch line' "$(stroke_only 40),\"kind\":\"path\",\"path\":{\"subpaths\":[
            {\"closed\":false,\"knots\":[[456,412],[456,612]]},
            {\"closed\":false,\"knots\":[{\"anchor\":[456,566],\"in\":[456,566],\"out\":[456,500],\"smooth\":false},{\"anchor\":[584,446],\"in\":[584,520],\"out\":[584,446],\"smooth\":false}]}]}"
        for node in '456,412' '456,612' '584,446'; do
            shape 'Branch node' "\"kind\":\"ellipse\",\"rect\":[$((${node%,*} - 46)),$((${node#*,} - 46)),92,92],\"fill\":\"$outline\",\"stroke\":null"
        done
    fi
    build 1024 "$name"
}

# The small sizes, hand-tuned on their own pixel grid: no branch and no plate seams, only a bold outline, the pale rim,
# the dome and the centre plate, with half-pixel coordinates so that 1 px strokes land on whole pixels.
make_16() {
    shape Rim "\"kind\":\"ellipse\",\"rect\":[0.5,0.5,15,15],\"fill\":\"$rim\",\"stroke\":{\"width\":1,\"color\":\"$outline\",\"align\":\"center\"}"
    shape Dome "\"kind\":\"ellipse\",\"rect\":[2,2,12,12],\"fill\":\"$dome\",\"stroke\":null"
    shape 'Centre plate' "\"kind\":\"path\",\"path\":{\"subpaths\":[{\"closed\":true,\"knots\":[[8,5],[10.5,6.5],[10.5,9.5],[8,11],[5.5,9.5],[5.5,6.5]]}]},\"fill\":\"$plate\",\"stroke\":{\"width\":1,\"color\":\"$outline\",\"align\":\"center\",\"join\":\"round\"}"
    build 16 kortoisegit-16
}

make_22() {
    shape Rim "\"kind\":\"ellipse\",\"rect\":[0.5,0.5,21,21],\"fill\":\"$rim\",\"stroke\":{\"width\":1,\"color\":\"$outline\",\"align\":\"center\"}"
    shape Dome "\"kind\":\"ellipse\",\"rect\":[3.5,3.5,15,15],\"fill\":\"$dome\",\"stroke\":{\"width\":1,\"color\":\"$outline\",\"align\":\"center\"}"
    shape 'Centre plate' "\"kind\":\"path\",\"path\":{\"subpaths\":[{\"closed\":true,\"knots\":[[11,7],[14.5,9],[14.5,13],[11,15],[7.5,13],[7.5,9]]}]},\"fill\":\"$plate\",\"stroke\":{\"width\":1,\"color\":\"$outline\",\"align\":\"center\",\"join\":\"round\"}"
    shape Seams "$(stroke_only 1),\"kind\":\"path\",\"path\":{\"subpaths\":[
        {\"closed\":false,\"knots\":[[11,7],[11,3.5]]},
        {\"closed\":false,\"knots\":[[14.5,9],[17.5,7.2]]},
        {\"closed\":false,\"knots\":[[14.5,13],[17.5,14.8]]},
        {\"closed\":false,\"knots\":[[11,15],[11,18.5]]},
        {\"closed\":false,\"knots\":[[7.5,13],[4.5,14.8]]},
        {\"closed\":false,\"knots\":[[7.5,9],[4.5,7.2]]}]}"
    build 22 kortoisegit-22
}

make_full kortoisegit branch
make_full kortoisegit-32 no-branch # the branch is only mush at 32 px
make_16
make_22

# ecm_install_icons names: <size>-apps-<icon>.png
photocraft-cli run "$out/kortoisegit-16.pcraft" --out "$out/16-apps-kortoisegit.png" >/dev/null
photocraft-cli run "$out/kortoisegit-22.pcraft" --out "$out/22-apps-kortoisegit.png" >/dev/null
photocraft-cli run "$out/kortoisegit-32.pcraft" --cmd image.imageSize --params '{"width":32,"height":32,"resample":"bicubic"}' \
    --out "$out/32-apps-kortoisegit.png" >/dev/null
for size in 48 64 128 256; do
    photocraft-cli run "$out/kortoisegit.pcraft" --cmd image.imageSize --params "{\"width\":$size,\"height\":$size,\"resample\":\"bicubic\"}" \
        --out "$out/$size-apps-kortoisegit.png" >/dev/null
done
