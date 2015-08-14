#!/bin/sh
input=$1
prefix=$2
shift 2
if [[ -z "$input" || -z "$prefix" ]]; then
  echo "usage: $0 <input> <prefix> [<options>]"
  exit 1
fi
mkdir -p $prefix
cmd="ffmpeg -i $input $* -vf scale=320:240,transpose=1 $prefix/${prefix}_%05d.bmp"
echo $cmd
$cmd
cmd="ffmpeg -i $input -vn -ar 15625 -ac 1 -acodec pcm_u8 ${prefix}.wav"
echo $cmd
$cmd
cmd="./ck_enc -a ${prefix}.wav $prefix/${prefix}"
echo $cmd
$cmd
mv $prefix/${prefix}.ck .
rm $prefix/*.bmp
rmdir $prefix/
