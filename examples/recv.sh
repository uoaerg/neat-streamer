gst-launch-1.0 -v \
	udpsrc port=5000 \
	! application/x-rtp \
	! rtph264depay \
	! decodebin \
	! autovideosink
