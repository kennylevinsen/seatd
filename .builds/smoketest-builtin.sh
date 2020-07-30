#!/bin/sh

res=0
#
# Run simpletest a few times
#
cnt=0
while [ "$cnt" -lt 5 ]
do
   echo "Simpletest run $cnt"
   if ! sudo LIBSEAT_BACKEND=builtin LIBSEAT_LOGLEVEL=debug SEATD_SOCK=./seatd.sock ./build/simpletest
   then
      echo "Simpletest failed"
      res=1
      break
   fi
   cnt=$((cnt+1))
done


exit $res
