raspivid -n -t 0 -o - | nc -u 127.0.0.1 6101 & 
                                               
gst-launch-1.0 udpsrc port=6101 \              
    ! h264parse \                              
    ! rtph264pay config-interval=1 pt=96 \     
    ! udpsink host=10.10.0.2 port=6969         
    #! udpsink host=127.0.0.1 port=6969        

exit

raspivid -t 0 -o - | \                         
    gst-launch-1.0 fdsrc \                     
        ! h264parse \                          
        ! rtph264pay config-interval=1 pt=96 \ 
        ! udpsink host=10.10.0.2 port=6969     
        #! udpsink host=127.0.0.1 port=6969    



gst-launch-1.0 v4l2src do-timestamp=true \
	! video/x-h264,width=640,height=480,framerate=30/1 \
	! h264parse ! rtph264pay config-interval=1 \
	! gdppay \
	! udpsink host=127.0.0.1 port=6901

exit

raspivid -t 0 -o - | gst-launch-1.0 fdsrc 
	! h264parse 
	! rtph264pay config-interval=1 pt=96 
	! gdppay 
	! udpsink host=127.0.0.1 port=6901

exit

gst-launch-1.0 v4l2src do-timestamp=true !
video/x-h264,width=640,height=480,framerate=30/1 ! h264parse ! rtph264pay
config-interval=1 ! gdppay ! udpsink host=SERVER_IP port=1234


raspivid -t 0 -o - | gst-launch-1.0 fdsrc ! h264parse ! rtph264pay config-interval=1 pt=96 ! gdppay ! tcpserversink host=SERVER_IP port=1234


gst-launch-1.0 -v wrappercamerabinsrc mode=2 \
	! video/x-raw, width=320, height=240\
	! x264enc \
	! rtph264pay  \
	! udpsink host=127.0.0.1 port=6969
