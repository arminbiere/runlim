#!/bin/sh
debug=no
prefix=/usr/local/bin
die () {
  echo "configure.sh: error: $*" 1>&2
  exit 1
}
msg () {
  echo "[configure.sh] $*"
}
while [ $# -gt 0 ]
do
  case "$1" in
    -h|--help)
      echo "usage: [CC=<compiler>] configure.sh [-h|--help|-g|--prefix=<install-dir>]"
      exit 0
      ;;
    -g) debug=yes;;
    --prefix=*)
      prefix=`echo "$1"|sed -e 's,^--prefix=,,'`
      [ -d "$prefix" ] || die "invalid directory in '$1'"
      ;;
    *) die "invalid option '$1' (try -h)";;
  esac
  shift
done
if [ x"$CC" = x ]
then
  COMPILE=gcc
else
  COMPILE="$CC"
fi
if [ x"$CFLAGS" = x ]
then
  case x"$COMPILE" in
    xclang*|xgcc*)
      COMPILE="$COMPILE -Wall"
      if [ $debug = yes ]
      then
	COMPILE="$COMPILE -g"
      else
	COMPILE="$COMPILE -O3 -DNDEBUG"
      fi
      ;;
    *)
      COMPILE="$COMPILE -W"
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
COMPILE="$COMPILE -DVERSION=\\\\\"$VERSION\\\\\""
msg "$COMPILE"
msg "installation prefix '$prefix'"
sed \
  -e "s#@PREFIX@#$prefix#" \
  -e "s#@COMPILE@#$COMPILE#" \
  makefile.in > makefile
