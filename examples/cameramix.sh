
#main feed from udp, local camera input(or testsrc, cause that is much easier)
gst-launch-1.0 videomixer name=mixer backgroud=checker \
        sink_0::xpos=0 sink_0::ypos=0 sink_0::zorder=1\
        sink_1::xpos=500 sink_1::ypos=10 sink_1::zorder=3\
	! glimagesink \
	videotestsrc pattern=smpte75 is-live=true ! video/x-raw,format=Y444,width=170,height=120 ! mixer.sink_1 \
	udpsrc address=0.0.0.0 port=6969 \
	! application/x-rtp \
	! rtph264depay \
	! decodebin \
	! video/x-raw,format=Y444,width=680,height=480 ! mixer.sink_0

exit 

	filesrc location=neat.png ! decodebin ! imagefreeze ! video/x-raw,format=Y444,width=640,height=480 ! mixer.sink_2 \

#send
gst-launch-1.0 -v \
	videotestsrc \
	! video/x-raw,format=Y444,width=680,height=480 \
	! x264enc \
	! rtph264pay \
	! udpsink host=127.0.0.1 port=5000

#recv
gst-launch-1.0 -v \
	udpsrc port=5004 \
	! application/x-rtp \
	! rtph264depay \
	! decodebin \
	! autovideosink

#send
gst-launch-1.0 -v \
   videotestsrc \
   ! x264enc \
   ! rtph264pay \
   ! udpsink host=127.0.0.1 port=5004

#working p in p video mixer using testsrc
gst-launch-1.0 videomixer name=mixer backgroud=checker \
        sink_0::xpos=0 sink_0::ypos=0 \
        sink_1::xpos=500 sink_1::ypos=10\
	! glimagesink \
	videotestsrc pattern=smpte ! video/x-raw,format=RGB,width=170,height=120 ! mixer.sink_1 \
	videotestsrc ! video/x-raw,format=RGB,width=680,height=480 ! mixer.sink_0
