#!/bin/sh
version=`cat VERSION`
rm -rf /tmp/runlim-$version
mkdir /tmp/runlim-$version
cp -p \
  runlim.c VERSION makefile.in configure.sh README NEWS LICENSE \
  /tmp/runlim-$version
cd /tmp
tar zcvf runlim-$version.tar.gz runlim-$version
rm -rf /tmp/runlim-$version
ls -l /tmp/runlim-$version.tar.gz
