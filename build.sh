#gcc -g -Wall udpbin.c -o udpbin -I/usr/local/include/gstreamer-1.0 -I/usr/local/lib/gstreamer-1.0/include -I/usr/local/include/glib-2.0 -I/usr/local/lib/glib-2.0/include -I/usr/local/include -pthread -L/usr/local/lib -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -lintl  

#might need this 
# export LD_LIBRARY_PATH=neat-streamer/neat/build:$LD_LIBRARY_PATH  

set -e

echo "compiling"
/usr/bin/cc  -g -DHAVE_SA_LEN -DHAVE_SIN6_LEN -DHAVE_SIN_LEN -DHAVE_SS_LEN \
	-DNEAT_LOG -I/usr/local/include -DHAVE_NETINET_SCTP_H \
	-DHAVE_SCTP_SEND_FAILED_EVENT -DHAVE_SCTP_EVENT_SUBSCRIBE -std=c99 -pedantic \
	-Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -g \
	-I/usr/local/lib/gstreamer-1.0/include -I/usr/local/include/glib-2.0 \
	-I/usr/local/lib/glib-2.0/include -I/usr/local/include \
	-o build/neat-streamer.o  -c neat-streamer.c -I/usr/local/include/gstreamer-1.0


echo "linking"
unamestr=`uname`
if [[ "$unamestr" == 'Darwin' ]]; then
	echo "MacOS"
	/usr/bin/cc -DHAVE_NETINET_SCTP_H -DHAVE_SCTP_SEND_FAILED_EVENT \
		-DHAVE_SCTP_EVENT_SUBSCRIBE -std=c99 -pedantic -Wall -Wextra -Werror \
		-Wno-unused-function -Wno-unused-parameter -g build/neat-streamer.o -o neat-streamer \
		neat/build/libneat.dylib /usr/local/lib/libuv.dylib /usr/local/lib/libldns.dylib \
		/usr/local/lib/libjansson.dylib \
		-L/usr/local/lib -Lneat/build/ -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 \
		-Wl,-rpath,/usr/home/tom/code/neat/build:/usr/local/lib 
elif [[ "$unamestr" == 'FreeBSD' ]]; then
	platform='freebsd'
	echo "FreeBSD"
	/usr/bin/cc -DHAVE_NETINET_SCTP_H -DHAVE_SCTP_SEND_FAILED_EVENT \
		-DHAVE_SCTP_EVENT_SUBSCRIBE -std=c99 -pedantic -Wall -Wextra -Werror \
		-Wno-unused-function -Wno-unused-parameter -g build/neat-streamer.o -o neat-streamer \
		neat/build/libneat.so /usr/local/lib/libuv.so /usr/local/lib/libldns.so \
		/usr/local/lib/libjansson.so \
		-L/usr/local/lib -Lneat/build/ -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -lintl \
		-Wl,-rpath,/usr/home/tom/code/neat/build:/usr/local/lib 
fi
