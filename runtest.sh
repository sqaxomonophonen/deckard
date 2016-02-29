#!/bin/sh
tmp=$(mktemp)
$* 2>&1 | tee $tmp
inbt=0
cat $tmp | while read line ; do
	case $line in
		"BACKTRACE BEGIN") inbt=1 ;;
		"BACKTRACE END") inbt=0 ;;
		*)
			if [ "$inbt" = "1" ] ; then
				addr=$(echo $line | perl -ne '/\[(0x[0-9a-fA-F]*)\]/ && print "$1\n"')
				if [ -n "$addr" ] ; then
					addr2line -e $1 $addr
				fi
			fi
			;;
	esac
done
rm $tmp
