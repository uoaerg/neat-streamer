gst-launch-1.0 -v \
	udpsrc port=6969 \
	! application/x-rtp \
	! rtph264depay \
	! decodebin \
	! autovideosink
