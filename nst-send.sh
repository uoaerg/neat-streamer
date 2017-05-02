#!/bin/bash

set -e

unamestr=`uname`

if [ "$unamestr" == 'Linux' ]; then
	raspivid -n -t 0 -o - | gst-launch-1.0 fdsrc ! udpsink host=127.0.0.1 port=6101 &
fi

./neat-streamer -h 10.10.0.2 -d
