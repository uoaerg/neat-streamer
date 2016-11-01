#include <gst/gst.h>

#include <gst/app/gstappsink.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define HOST "127.0.0.1"
#define PORT "5000"

int
main (int argc, char *argv[])
{
	GstElement *pipeline, *sink;
	GstSample *sample;
	gchar *descr;
	GError *error = NULL;
	GstStateChangeReturn ret;
	GstMapInfo map;

	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;

	gst_init (&argc, &argv);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo(HOST, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("udpbin: socket");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "udpbin: failed to create socket\n");
		return 2;
	}

	/* set up the socket and the address */
	if( (sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		perror("creating socket");
		exit(-1);
	}

	descr =
		g_strdup_printf ("videotestsrc ! x264enc ! rtph264pay ! "
		" appsink name=sink caps=\"%s" "\"", gst_caps_to_string(GST_CAPS_ANY));
 
	pipeline = gst_parse_launch (descr, &error);

	if (error != NULL) {
		g_print ("could not construct pipeline: %s\n", error->message);
		g_clear_error (&error);
		exit (-1);
	}

	/* get sink */
	sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

	ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
	switch (ret) {
	case GST_STATE_CHANGE_FAILURE:
		g_print ("failed to play the file\n");
		exit (-1);
	case GST_STATE_CHANGE_NO_PREROLL:
		/* for live sources, we need to set the pipeline to PLAYING before we can
		* receive a buffer. We don't do that yet */
		g_print ("live sources not supported yet\n");
		exit (-1);
	default:
		break;
	}
	/* This can block for up to 5 seconds. If your machine is really overloaded,
	 * it might time out before the pipeline prerolled and we generate an error. A
	 * better way is to run a mainloop and catch errors there. 
	 */
	ret = gst_element_get_state (pipeline, NULL, NULL, 5 * GST_SECOND);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print ("failed to play the file\n");
		exit (-1);
	}

	int cnt = 0;

	while(1) {
		g_signal_emit_by_name (sink, "pull-sample", &sample, NULL);	/* blocks */

	
		if (sample) {
			GstBuffer *buffer;
			GstCaps *caps;

			caps = gst_sample_get_caps (sample);
			if (!caps) {
			  g_print ("could not get snapshot format\n");
			  exit (-1);
			}

			buffer = gst_sample_get_buffer (sample);

			/* Mapping a buffer can fail (non-readable) */
			if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
				int res = sendto(sockfd, map.data, map.size, 0, p->ai_addr, p->ai_addrlen);

				if(res == -1) {
					perror("sendto");
				} else {
					if(cnt++ > 100) {
						printf(".");
						fflush(stdout);
						cnt = 0;
					}
				}

				gst_buffer_unmap (buffer, &map);
			}
		} else {
			g_print ("could not get frame\n");
		}

		gst_sample_unref(sample);
	}

	/* cleanup and exit */
	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_object_unref (pipeline);

	exit (0);
}
