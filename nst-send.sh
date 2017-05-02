#!/bin/sh

set -e

unamestr=`uname`

if [ "$unamestr" == 'Linux' ]; then
	raspivid -n -t 0 -o - | nc -u 127.0.0.1 6101 & 
fi

./neat-streamer -h 10.10.0.2 -d
