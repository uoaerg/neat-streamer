gst-launch-1.0 -v \
   videotestsrc \
   ! x264enc \
   ! rtph264pay \
   ! udpsink host=127.0.0.1 port=6969
