#!/bin/sh
debug=no
while [ $# -gt 0 ]
do
  case $1 in
    -h|--help) echo "usage: configure.sh [-h|--help|-g]"; exit 0;;
    -g) debug=yes;;
    *) echo "*** configure.sh: invalid option '$1' (try -h)"; exit 1;;
  esac
  shift
done
[ x"$CC" = x ] && COMPILE=gcc
if [ x"$CFLAGS" = x ]
then
  case x"$COMPILE" in
    xgcc)
      COMPILE="$COMPILE -Wall"
      if [ $debug = yes ]
      then
	COMPILE="$COMPILE -g"
      else
	COMPILE="$COMPILE -O3 -DNDEBUG"
      fi
      ;;
    *)
      COMPILE="-W"
      if [ $debug = yes ]
      then
	COMPILE="$COMPILE -g"
      else
	COMPILE="$COMPILE -O -DNDEBUG"
      fi
      ;;
  esac
fi
VERSION="`cat VERSION`"
COMPILE="$COMPILE -DVERSION=$VERSION"
if [ -f /proc/sys/kernel/pid_max ]
then
  PID_MAX=`cat /proc/sys/kernel/pid_max`
  COMPILE="$COMPILE -DPIDX_MAX=$PID_MAX"
fi
echo "$COMPILE"
sed -e "s,@COMPILE@,$COMPILE," makefile.in > makefile
