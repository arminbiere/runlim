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
echo "[configure.sh] version `cat VERSION`"
[ x"$CC" = x ] && CC=gcc
if [ x"$CFLAGS" = x ]
then
  case x"$CC" in
    xgcc)
      CFLAGS="-Wall"
      if [ $debug = yes ]
      then
	CFLAGS="$CFLAGS -g"
      else
	CFLAGS="$CFLAGS -O3 -DNDEBUG"
      fi
      ;;
    *)
      CFLAGS="-W"
      if [ $debug = yes ]
      then
	CFLAGS="$CFLAGS -g"
      else
	CFLAGS="$CFLAGS -O -DNDEBUG"
      fi
      ;;
  esac
fi
echo "[configure.sh] $CC $CFLAGS"
rm -f makefile
sed \
-e "s,@CC@,$CC," \
-e "s,@CFLAGS@,$CFLAGS," \
makefile.in > makefile
echo "[configure.sh] generated makefile"
