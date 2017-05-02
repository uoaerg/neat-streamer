raspivid -n -t 0 -o - | gst-launch-1.0 fdsrc ! udpsink host=127.0.0.1 port=6101 &  

gst-launch-1.0 udpsrc port=6101 \
    ! h264parse \
    ! rtph264pay config-interval=1 pt=96 \
    ! udpsink host=10.10.0.2 port=6969         
exit
