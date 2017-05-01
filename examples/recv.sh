export GST_DEBUG_FILE=gstrecv.log 
export GST_DEBUG=5 
export GST_DEBUG_COLOR_MODE=off
gst-launch-1.0 -v \
	udpsrc port=6969 \
	! application/x-rtp \
	! rtph264depay \
	! decodebin \
	! autovideosink
