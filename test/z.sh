#!/bin/sh
echo $0
head -4 /proc/$$/stat|cut -d ' ' -f 1-6
./c.sh &
./c.sh &
