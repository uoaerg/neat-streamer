#gcc -g -Wall udpbin.c -o udpbin -I/usr/local/include/gstreamer-1.0 -I/usr/local/lib/gstreamer-1.0/include -I/usr/local/include/glib-2.0 -I/usr/local/lib/glib-2.0/include -I/usr/local/include -pthread -L/usr/local/lib -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -lintl  

set -e

echo "compiling"
/usr/bin/cc  -g -DHAVE_SA_LEN -DHAVE_SIN6_LEN -DHAVE_SIN_LEN -DHAVE_SS_LEN \
	-DNEAT_LOG -I/usr/local/include -DHAVE_NETINET_SCTP_H \
	-DHAVE_SCTP_SEND_FAILED_EVENT -DHAVE_SCTP_EVENT_SUBSCRIBE -std=c99 -pedantic \
	-Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -g \
	-I/usr/local/lib/gstreamer-1.0/include -I/usr/local/include/glib-2.0 \
	-I/usr/local/lib/glib-2.0/include -I/usr/local/include \
	-o neat-streamer.c.o  -c neat-streamer.c -I/usr/local/include/gstreamer-1.0

echo "linking"
/usr/bin/cc -DHAVE_NETINET_SCTP_H -DHAVE_SCTP_SEND_FAILED_EVENT \
	-DHAVE_SCTP_EVENT_SUBSCRIBE -std=c99 -pedantic -Wall -Wextra -Werror \
	-Wno-unused-function -Wno-unused-parameter -g neat-streamer.c.o -o neat-streamer \
	neat/build/libneat.so /usr/local/lib/libuv.so /usr/local/lib/libldns.so \
	/usr/local/lib/libjansson.so \
	-pthread -L/usr/local/lib -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -lintl \
	-Wl,-rpath,/usr/home/tom/code/neat/build:/usr/local/lib 
