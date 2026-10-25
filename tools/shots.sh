#!/bin/sh
# SYN-BIOGENESIS-3D: the README's pictures, made again.
#
#   tools/shots.sh [NAME...]     every picture in docs/screenshots, or those named
#
# Draws the window off the screen (SDL_VIDEODRIVER=offscreen) with a home of
# its own, so your settings, library, worlds and arena are left alone, in
# the SYN-OS-M141 theme and the Biological look. The worlds are made
# headless first: seed 104 in the default box to time 300, and the other
# shapes to the same time. The beings come from the sibling SYN-BIOGENESIS
# checkout and its library, as in tools/ecology.py, and the picture of 0.9
# on its plane from SYN-BIOGENESIS itself. Needs ImageMagick; pngquant, if
# installed, makes the files smaller.
set -e
cd "$(dirname "$0")/.."
make -s
out=docs/screenshots
bin=$PWD/syn-biogenesis-3d
gallery=$PWD/../SYN-BIOGENESIS/tools/javaref/download/gallery
library=$HOME/.config/syn-biogenesis/beings/0.9
two_d=$PWD/../SYN-BIOGENESIS/syn-biogenesis
dir=$(mktemp -d /tmp/syn3d.XXXXXX)
trap 'rm -rf "$dir"' EXIT
export HOME="$dir" SDL_VIDEODRIVER=offscreen
unset XDG_CONFIG_HOME
cfg=$HOME/.config/syn-biogenesis-3d
mkdir -p "$out" "$cfg/beings/Imported" "$HOME/.config/syn-os" "$dir/w" "$dir/b"
echo SYN-OS-M141 >"$HOME/.config/syn-os/current-theme"
names=" $* "

want() {
    [ "$names" = "  " ] && return 0
    case $names in *" $1 "*) return 0 ;; esac
    return 1
}

look() {
    printf 'theme=SYN-OS-M141\nlook=%s\n' "$1" >"$cfg/settings"
}

# shot NAME COMMAND...: the window's picture, kept as $dir/NAME.ppm
shot() {
    name=$1
    shift
    want "$name" || return 0
    "$@" --shot "$dir/$name.ppm" >"$dir/$name.log" 2>&1 || {
        echo "$name: failed (see below)" >&2
        cat "$dir/$name.log" >&2
        return 1
    }
}

# keep NAME [MAGICK OPTIONS...]: $dir/NAME.ppm as docs/screenshots/NAME.png
keep() {
    name=$1
    shift
    [ -f "$dir/$name.ppm" ] || return 0
    magick "$dir/$name.ppm" "$@" "$out/$name.png"
    if command -v pngquant >/dev/null; then
        pngquant --quality 70-95 --speed 1 --force --ext .png "$out/$name.png"
    fi
    echo "$out/$name.png"
}

# a being of six arms, flat, then bent, then as a cone and as an orb
being() {
    cat >"$dir/b/$1.bgg" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<genetic_code symmetry="6" mirror="no" disperse="yes" light="0.6" carbon="0.1" plants="0" hunters="-0.5" dead="0.2" crowd="-0.3" height="0.8" $2>
	<gene length="16" theta="0.5" $3 color="green" />
	<gene length="11" theta="5.4" $4 color="cyan" />
	<gene length="13" theta="1.3" $5 color="red" />
</genetic_code>
EOF
}
being flat 'plan="disk"' '' '' ''
being bent 'plan="disk"' 'pitch="0.6"' 'pitch="-0.7"' 'pitch="0.5"'
being cone 'plan="cone" cone="0.7"' '' '' ''
being orb 'plan="orb"' '' '' ''

# the worlds, headless: the default box, and the shapes of water
world() {
    name=$1
    shift
    "$bin" --headless --frames 76800 --save "$dir/w/$name.b3w" "$@" >/dev/null 2>&1
}
world default --seed 104 &
world pond --seed 17 --size 1000 1000 64 &
world cube --seed 5 --size 400 400 400 &
world column --seed 5 --size 300 300 900 &
world slab --seed 5 --size 1000 1000 32 --pitch 0 &
wait
w=$dir/w/default.b3w

look biological
# the world, under the surface, and an organism selected
shot world "$bin" --load "$w" --screen world --camera 20 8 380 --seconds 5.3
shot organism "$bin" --load "$w" --screen world --select -1 --camera 30 15 140 --seconds 5.3
shot lab "$bin" --load "$w" --screen laboratory --select -1 --seconds 5.3
for s in default pond cube column slab; do
    shot "shape-$s" "$bin" --load "$dir/w/$s.b3w" --screen world --seconds 5.3
done

# the screens: statistics on a world watched from genesis, run side by side
for tab in overview charts epochs timeline; do
    want "stats-$tab" && shot "stats-$tab" env SYN3D_SHOT_TAB=$tab \
        "$bin" --seed 104 --warm 300 --screen statistics --seconds 5.3 &
done
want stats-water && shot stats-water env SYN3D_SHOT_TAB=charts SYN3D_SCRIPT="wait 20; move 720 600; wheel -12; move 10 880" \
    "$bin" --seed 104 --warm 300 --screen statistics --seconds 5.3 &
want stats-family && shot stats-family env SYN3D_SHOT_TAB=family SYN3D_SHOT_FAMILY=living SYN3D_SHOT_FOCUS=1 \
    "$bin" --seed 104 --warm 300 --screen statistics --seconds 5.3 &
wait
shot arena env SYN3D_SHOT_ARENA=6,60,1000 SYN3D_SHOT_ARENA_VIEW=battles "$bin" --screen arena --seconds 5.3
shot arena-tree env SYN3D_SHOT_ARENA=12,40,1000 SYN3D_SHOT_ARENA_VIEW=tree "$bin" --screen arena --seconds 5.3
# the telescope finds the seed of a world seed 7 made
shot telescope env SYN3D_SCRIPT="wait 30; click 124 276; wait 10; click 140 332" \
    "$bin" --seed 7 --warm 20 --screen telescope --seconds 40
shot menu "$bin" --load "$w" --screen menu --seconds 5.3
shot options "$bin" --load "$w" --screen options --seconds 5.3
shot parameters "$bin" --load "$w" --screen parameters --seconds 5.3

# beings, in the laboratory: the body plans tilted to show them, and 0.9's
# beings as they arrive, from above
crop="-crop 420x300+907+210 +repage"
for b in flat bent cone orb; do
    shot "body-$b" env SYN3D_SCRIPT="wait 3; down 1120 300; move 1120 325; move 1120 350; up 1120 350; move 700 880" \
        "$bin" --organisms 0 --import "$dir/b/$b.bgg" --select -1 --screen laboratory --seconds 1.5
done
for b in "$gallery/gallery_4.bgg" "$gallery/gallery_6.bgg" "$library/Forged/seed 20908.bgg" \
    "$library/Saved/CHAMPION (2).bgg" "$library/Imported/ultimate.bgg" "$library/Saved/#305.bgg"; do
    [ -f "$b" ] || continue
    cp "$b" "$cfg/beings/Imported/"
    n=$(basename "$b" .bgg | tr -cs 'A-Za-z0-9_\n' '-' | tr 'A-Z' 'a-z' | sed 's/^-*//; s/-*$//')
    shot "being-$n" "$bin" --organisms 0 --import "$b" --select -1 --screen laboratory
done
cp "$dir"/b/*.bgg "$cfg/beings/Imported/"
shot library env SYN3D_SCRIPT="wait 30; click 100 256" "$bin" --load "$w" --screen library --seconds 5.3

# the looks, from a little further off
for l in classic galaxy digital biological; do
    look "$l"
    shot "look-$l" "$bin" --load "$w" --screen world --camera 35 18 520 --seconds 5.3
done
look biological

# Biogenesis 0.9 on its plane, seed 104 at time 300, in SYN-BIOGENESIS's Biological look
if [ -x "$two_d" ] && want plane; then
    env XDG_CONFIG_HOME="$dir/2d" SYN_BIOGENESIS_INTERFACE=synos SYN_BIOGENESIS_LOOK=biological \
        SYN_BIOGENESIS_THEME=SYN-OS-M141 SYN_BIOGENESIS_SHOT="$dir/plane.png" SYN_BIOGENESIS_SHOT_SIM=76800 \
        SYN_BIOGENESIS_SHOT_SCREEN=world "$two_d" 104 >/dev/null 2>&1
    magick "$dir/plane.png" "$dir/plane.ppm"
fi

for n in world organism lab shape-default shape-pond shape-cube shape-column shape-slab \
    stats-overview stats-charts stats-water stats-epochs stats-family stats-timeline \
    arena arena-tree telescope menu options parameters library \
    look-classic look-galaxy look-digital look-biological plane; do
    keep "$n"
done
for f in "$dir"/body-*.ppm "$dir"/being-*.ppm; do
    [ -f "$f" ] || continue
    # shellcheck disable=SC2086
    keep "$(basename "$f" .ppm)" $crop
done
