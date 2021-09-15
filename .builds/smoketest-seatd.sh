#!/bin/sh

# Devices that exist on sr.ht
if [ -e "/dev/input/event0" ]
then
   file="/dev/input/event0"
elif [ -e "/dev/dri/card0" ]
then
   file="/dev/dri/card0"
else
   echo "No useful device file found"
   exit 1
fi

export SEATD_LOGLEVEL=debug
#
# Run simpletest a few times
#
cnt=0
while [ "$cnt" -lt 2 ]
do
   echo "Simpletest run $((cnt+1))"
   if ! sudo -E ./build/seatd-launch ./build/simpletest $file
   then
      echo "Simpletest failed"
      exit 1
   fi
   cnt=$((cnt+1))
done

echo "smoketest-seatd completed"
