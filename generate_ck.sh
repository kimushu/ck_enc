#!/bin/sh
input=$1
output=$2
shift 2
if [[ -z "$input" || -z "$output" ]]; then
  echo "usage: $0 <input> <output> [<options_for_ckenc>]"
  exit 1
fi
tmp="tmp_${output##*/}"
mkdir -p $tmp
cmd="ffmpeg -i $input -r 10 -vf scale=320:240,transpose=1 $tmp/video_%05d.bmp"
echo $cmd
$cmd
wav="$tmp/audio.wav"
cmd="ffmpeg -i $input -y -vn -ar 15625 -ac 1 -acodec pcm_u8 $wav"
echo $cmd
$cmd
cmd="./ck_enc -a $wav $* $tmp/video"
echo $cmd
$cmd
mv $tmp/video.ck $output
rm $tmp/audio.wav
rm $tmp/video_*.bmp
rmdir $tmp
