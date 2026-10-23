#!/bin/sh
# SYN-BIOGENESIS-3D: film a world into a video.
#
#   tools/film.sh OUT.mp4 [film options]     (tools/film --help lists them)
#
# Runs tools/film and pipes its frames through ffmpeg, writing the caption
# (time, alive, dead...) over each frame in SYN Terminal when the sibling
# SYN-BIOGENESIS checkout has it, or DejaVu Sans Bold.
set -e
cd "$(dirname "$0")/.."
[ $# -ge 1 ] || { echo "usage: tools/film.sh OUT.mp4 [film options]" >&2; exit 2; }
out=$1
shift
make -s tools/film
w=1280
h=720
want=""
for a in "$@"; do
    case $want in
    w) w=$a; want=h; continue ;;
    h) h=$a; want=""; continue ;;
    esac
    [ "$a" = "--frame" ] && want=w
done
font=../SYN-BIOGENESIS/assets/fonts/SYNTerminal-Bold.ttf
[ -f "$font" ] || font=$(fc-match -f '%{file}' 'DejaVu Sans:bold')
cap=$(mktemp)
./tools/film --caption "$cap" "$@" |
    ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s "${w}x${h}" -r 30 -i - \
        -vf "drawtext=fontfile=$font:textfile=$cap:reload=1:expansion=none:x=28:y=24:fontsize=$((h / 30)):fontcolor=0xd8dee9:line_spacing=10:box=1:boxcolor=0x0b0e14@0.55:boxborderw=12" \
        -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p "$out"
rm -f "$cap" "$cap.tmp"
echo "$out"
