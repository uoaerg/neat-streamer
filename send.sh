gst-launch-1.0 -v \
   videotestsrc \
   ! x264enc \
   ! rtph264pay \
   ! udp host=127.0.0.1 port=5000
