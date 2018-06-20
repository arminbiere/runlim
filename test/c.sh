#!/bin/sh
echo $0
head -4 /proc/$$/stat|cut -d ' ' -f 1-6
i=0
while [ $i -lt 2 ]
do
  if [ $i = 1 ]
  then
    i=0
  else
    i=1
  fi
done
