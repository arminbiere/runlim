#!/bin/sh
echo $0
head -4 /proc/$$/stat|cut -d ' ' -f 1-6
sleep 1
./c.sh &
./c.sh &
./c.sh &
i=0
while true;
do
  i=`expr $i + 1`
done
