gst-launch-1.0 -v wrappercamerabinsrc mode=2 \
	! video/x-raw, width=320, height=240\
	! x264enc \
	! rtph264pay  \
	! udpsink host=127.0.0.1 port=6969
