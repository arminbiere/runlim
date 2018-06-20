#!/bin/sh
echo $0
head -4 /proc/$$/stat|cut -d ' ' -f 1-6
i=0
while true;
do
  i=`expr $i + 1`
done
