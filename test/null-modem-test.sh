#!/bin/bash

# Test basic serial port functionality under Linux.
# Author: Tom Szilagyi <tomszilagyi@gmail.com>
# This file is placed in the public domain.
#
# It is assumed that two devices are available and connected with each
# other via a null-modem cable:
#
# GND <---> GND
# TxD <---> RxD
# RxD <---> TxD
# CTS <---> RTS
# RTS <---> CTS

DEBUG=1

DEV0=/dev/ttyUSB0
DEV1=/dev/ttyUSB1

IN0=/usr/share/dict/american-english

function dbg {
    if [ $DEBUG -ne 0 ] ; then
	echo $@
    fi
}

function test_file {
    if [ ! -f $1 ] ; then
	echo error: $1 is not a regular file
	exit -1
    fi
    dbg $1 regular OK
}

function test_chardev {
    if [ ! -c $1 ] ; then
	echo error: $1 is not a character device
	exit -1
    fi
    dbg $1 chardev OK
}

function set_serial {
    stty -F $1 115200 crtscts cs8
    #stty -F $1 230400 crtscts cs8
    #stty -F $1 460800 crtscts cs8
    #stty -F $1 921600 crtscts cs8
    #stty -F $1 1500000 crtscts cs8
    #stty -F $1 3000000 crtscts cs8
    #stty -F $1 6000000 crtscts cs8
    if [ $? -ne 0 ] ; then
	exit -1
    fi
}

function get_file_size {
    wc -c $1 | cut -d ' ' -f 1
}

function compare_output {
    cmp $1 $2
    if [ $? -ne 0 ] ; then
	echo Output $2 differs from input $1
    else
	dbg Output $2 OK
    fi
}

test_file $IN0
test_chardev $DEV0
test_chardev $DEV1

set_serial $DEV0
set_serial $DEV1

IN1=in1.txt
if [ ! -f $IN1 ] ; then
    # generate a file identical in size, but with different content
    # to be sent simultaneously in the other direction
    cat $IN0 | tr '0123456789QWERTZUIOPASDFGHJKLYXCVBNMqwertzuiopasdfghjklyxcvbnm' \
	'9876543210MNBVCXYLKJHGFDSAPOIUZTREWQmnbvcxylkjhgfdsapoiuztrewq' > $IN1
    dbg Input file generated.
fi

OUT0=out0.txt
OUT1=out1.txt
rm -f $OUT0 $OUT1

( cat $IN0 > $DEV0 ) &
( cat $IN1 > $DEV1 ) &
( cat $DEV1 > $OUT1 ) &
( cat $DEV0 > $OUT0 ) &

SIZE_IN0=$(get_file_size $IN0)
SIZE_OUT1=$(get_file_size $OUT1)
SIZE_OUT0=$(get_file_size $OUT0)
while [ $SIZE_OUT1 -lt $SIZE_IN0 ] || [ $SIZE_OUT0 -lt $SIZE_IN0 ]; do
    SIZE_OUT1=$(get_file_size $OUT1)
    SIZE_OUT0=$(get_file_size $OUT0)
    dbg recvd $SIZE_OUT0 $SIZE_OUT1 of $SIZE_IN0
    sleep 5
done

compare_output $IN0 $OUT1
compare_output $IN1 $OUT0

# get rid of any leftover cat processes
CPIDS=`ps --ppid=$$ | grep ca[t] | cut -d ' ' -f 1`
for cpid in $CPIDS; do
	dbg "killing cat (pid=$cpid)"
	kill $cpid
done
