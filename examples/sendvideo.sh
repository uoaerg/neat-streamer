gst-launch-1.0 -v filesrc location=test.mp4 \
	! decodebin \
	! x264enc \
	! rtph264pay  \
	! udpsink host=127.0.0.1 port=6969
