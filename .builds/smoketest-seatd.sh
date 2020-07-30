#!/bin/sh

#
# Start seatd
#
[ -f seatd.sock ] && sudo rm seatd.sock
sudo LIBSEAT_LOGLEVEL=debug SEATD_SOCK=./seatd.sock ./build/seatd &

# seatd is started in the background, so wait for it to come alive
cnt=0
while ! [ -e ./seatd.sock ] && [ "$cnt" -lt 10 ]
do
   sleep 0.1
   cnt=$((cnt+1))
done

if ! [ -e ./seatd.sock ]
then
   echo "seatd socket not found"
   exit 1
fi

sudo chmod 777 ./seatd.sock

#
# Run simpletest a few times
#
cnt=0
while [ "$cnt" -lt 5 ]
do
   echo "Simpletest run $cnt"
   if ! LIBSEAT_LOGLEVEL=debug SEATD_SOCK=./seatd.sock ./build/simpletest
   then
      echo "Simpletest failed"
      sudo killall seatd
      exit 1
   fi
   cnt=$((cnt+1))
done

#
# Wait for it to shut down
#
sudo killall seatd 2>/dev/null
