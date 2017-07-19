#!/bin/sh
# Retry Challanging Thursdays submissions
#
# Usage: retest.sh ctxy <ctxy.stat

while read ln
do
	SNAME="`echo $ln | cut -d ' ' -f1`"
	STIME="`echo $ln | cut -d ' ' -f2`"
	SFILE="`echo logs/$1-$STIME-$SNAME*`"
	SLANG="`echo $SFILE | sed 's/^.*\.//'`"
	echo -n "$SNAME	$STIME	"
	./test $1 $SFILE $SLANG
done
