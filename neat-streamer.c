#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <uv.h>

#include "neat/neat.h"
#include "neat/neat_internal.h"

/**********************************************************************

    neat streaming app
    client [OPTIONS] HOST PORT NOT IMPLEMENTED

**********************************************************************/


static struct neat_flow_operations ops;
static struct neat_ctx *ctx = NULL;
static struct neat_flow *main_flow = NULL;
static struct neat_flow *proxy_flow = NULL;
static int want = 0;
static int connected = 0;

GstElement *global_pipeline;

uv_prepare_t *prepare_handle;

static char *config_property = "{\
    \"transport\": [\
        {\
            \"value\": \"UDP\",\
            \"precedence\": 1\
        }\
    ]\
}";\

static uint32_t config_port = 6969;
static uint16_t config_log_level = 0;
static uint32_t config_buffer_size_max = 1400;
static char *config_pem_file = NULL;

static uint8_t camerasrc = 0;
static uint8_t displaysink = 0;
static uint8_t initiator = 0;
static uint8_t duplex = 0;
static uint8_t happy = 0;

static GstClockTime basetimestamp = 0;

GstSample *sample;

struct neat_streamer {
/* needs to capture a mode, gstreamer pipelines, protocol state, tons of shit*/
    unsigned char *buffer;
    uint32_t buffer_size;
    uint32_t buffer_alloc;

	GstElement *camerapipeline;			// Not sure about these
	GstElement *displaypipeline;		// Not sure about these
	GstAppSink *appsink;
	GstAppSrc  *appsrc;
	GstBuffer  *gst_buffer;
};

static neat_error_code on_all_written(struct neat_flow_operations *opCB);
int read_file(const char *, const char **);

GstAppSink *setupvideosender();
GstAppSrc *setupvideoreceiver();
void pump_g_loop(uv_prepare_t *handle);

struct neat_streamer *alloc_neat_streamer(void);
void free_neat_streamer(struct neat_streamer *);
static void feed_pipeline(struct neat_streamer *);

struct neat_streamer *
alloc_neat_streamer()
{
    struct neat_streamer *nst;

    if ((nst = calloc(1, sizeof(struct neat_streamer))) == NULL) {
        goto out;
    }

	nst->buffer_size = 0;
	nst->buffer_alloc = config_buffer_size_max;

    if ((nst->buffer = calloc(nst->buffer_alloc, sizeof(unsigned char))) == NULL) {
        goto out;
    }

    return nst;

out:
    if (nst == NULL)
        return NULL;

    free(nst->buffer);
    free(nst);

    return NULL;
}

void
free_neat_streamer(struct neat_streamer *nst)
{
	free(nst->buffer);
    free(nst);
}

GstAppSink *
setupvideosender()
{
	GstElement *pipeline, *appsink;
	gchar *descr;
	GError *error = NULL;
	GstStateChangeReturn ret;
/*
	descr =
		g_strdup_printf("videotestsrc ! x264enc ! rtph264pay ! "
		" appsink name=sink caps=\"%s" "\"", gst_caps_to_string(GST_CAPS_ANY));
*/

	descr =
		g_strdup_printf(
		"wrappercamerabinsrc mode=2 ! video/x-raw, width=320, height=240 !"
		" x264enc ! rtph264pay !"
		" appsink name=sink caps=\"%s" "\"", gst_caps_to_string(GST_CAPS_ANY));

	pipeline = gst_parse_launch(descr, &error);

	if (error != NULL) {
		g_print ("could not construct pipeline: %s\n", error->message);
		g_clear_error (&error);
		exit (-1);
	}

	/* get sink */
	appsink = gst_bin_get_by_name(GST_BIN (pipeline), "sink");

	ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
	switch (ret) {
	case GST_STATE_CHANGE_FAILURE:
		g_print ("failed to play the file\n");
		exit (-1);
	case GST_STATE_CHANGE_NO_PREROLL:
		/* for live sources, we need to set the pipeline to PLAYING before we
		 * can receive a buffer. We don't do that yet */
		g_print ("live sources not supported yet\n");
		exit (-1);
	default:
		break;
	}

	/* This can block for up to 5 seconds. If your machine is really
	 * overloaded, it might time out before the pipeline prerolled and we
	 * generate an error. A better way is to run a mainloop and catch errors
	 * there. 
	 */
	ret = gst_element_get_state(pipeline, NULL, NULL, 5 * GST_SECOND);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print("failed to play source\n");
		exit(-1);
	}

	return (GstAppSink *)appsink;
}

void 
pump_g_loop(uv_prepare_t *handle)
{
	g_main_context_iteration(g_main_context_default(), FALSE);
}

static void 
cb_need_data(GstElement *appsrc, guint unused_size, gpointer user_data) 
{
    if (config_log_level >= 1) {
		fprintf(stdout, "%s:%d\n", __func__, __LINE__);
    }
	want++;
}

GstAppSrc *
setupvideoreceiver()
{
	fprintf(stdout, "%s:%d\n", __func__, __LINE__);
	GstElement *pipeline, *appsrc;

/*
* gst-launch-1.0 -v \
*  udpsrc port=6969 \
*  ! application/x-rtp \
*  ! rtph264depay \
*  ! decodebin \
*  ! autovideosink
*/
	gchar *descr;
	GError *error = NULL;

	descr = g_strdup_printf(
		//"appsrc name=src is-live=true ! application/x-rtp ! rtph264depay ! decodebin ! autovideosink");
	//	"appsrc name=src is-live=true "
		
		"udpsrc port=6900 "	
		"! application/x-rtp, media=(string)video, clock-rate=(int)90000,encoding-name=(string)H264"
		"! rtph264depay "
		"! decodebin "
		"! autovideosink");

		//"! videorate "
		//"! filesink location=file.out");
 
	pipeline = gst_parse_launch(descr, &error);

	if (error != NULL) {
		g_print ("could not construct pipeline: %s\n", error->message);
		g_clear_error (&error);
		exit (-1);
	}

	appsrc = gst_bin_get_by_name(GST_BIN (pipeline), "src");

	/* setup appsrc */
	/*
	g_object_set (G_OBJECT (appsrc),
		"stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
		"format", GST_FORMAT_TIME,
		"is-live", TRUE,
		NULL);
	g_signal_connect (appsrc, "need-data", G_CALLBACK (cb_need_data), NULL);
	*/
	/* play */
	fprintf(stdout, "%s:%d Setting play on pipeline\n", __func__, __LINE__);

	GstStateChangeReturn ret;
	ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);

	switch (ret) {
	case GST_STATE_CHANGE_FAILURE:
		g_print ("failed to play the file\n");
		exit (-1);
	case GST_STATE_CHANGE_NO_PREROLL:
		/* for live sources, we need to set the pipeline to PLAYING before we
		 * can receive a buffer. We don't do that yet */
		g_print ("live sources not supported yet\n");
		exit (-1);
	case GST_STATE_CHANGE_SUCCESS:
		fprintf(stdout, "%s:%d play, IT WORKED!\n", __func__, __LINE__);
		break;
	case GST_STATE_CHANGE_ASYNC :
		fprintf(stdout, "%s:%d play, async!\n", __func__, __LINE__);
		break;
	default:
		fprintf(stdout, "%s:%d Setting play on pipeline, something \n", __func__, __LINE__);
		break;
	}

	global_pipeline = pipeline;

	return (GstAppSrc *)appsrc;
}

#include <ctype.h>
#include <stdio.h>

void 
hexdump(void *ptr, int buflen) 
{
  unsigned char *buf = (unsigned char*)ptr;
  int i, j;
  for (i=0; i<buflen; i+=16) {
    printf("%06x: ", i);
    for (j=0; j<16; j++) 
      if (i+j < buflen)
        printf("%02x ", buf[i+j]);
      else
        printf("   ");
    printf(" ");
    for (j=0; j<16; j++) 
      if (i+j < buflen)
        printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
    printf("\n");
  }
}

static void
feed_pipeline(struct neat_streamer *nst)
{
    if (config_log_level >= 1) {
		fprintf(stdout, "%s:%d\n", __func__, __LINE__);
    }

	GstFlowReturn ret;
	GstElement *element = NULL;

	if(!want) {
		return;
	}

	if(want > 1) {
		fprintf(stdout, "%s:%d %d\n", __func__, __LINE__, want);
	}
	want = 0;

	GstBuffer *buffer = gst_buffer_new_allocate(NULL, nst->buffer_size, NULL);

	gst_buffer_fill(buffer, 0, nst->buffer, nst->buffer_size);

	//GstMapInfo map;
	//gst_buffer_map (buffer, &map, GST_MAP_READ);

	//fprintf(stdout, "%s:%d %s%lu\n", __func__, __LINE__, "Buffer Size: ", map.size);
	//hexdump(map.data, map.size); 

    if (basetimestamp == 0) {
		/* https://github.com/GStreamer/gst-rtsp-server/blob/fb7833245de53bd6e409f5faf228be7899ce933f/gst/rtsp-server/rtsp-stream.c#L3167
		 * Take current running_time. This timestamp will be put on the first
		 * buffer of each stream because we are a live source and so we
		 * timestamp with the running_time. When we are dealing with TCP, we
		 * also only timestamp the first buffer (using the DISCONT flag)
		 * because a server typically bursts data, for which we don't want to
		 * compensate by speeding up the media. The other timestamps will be
		 * interpollated from this one using the RTP timestamps. 
		 */
		fprintf(stdout, "%s:%d %s\n", __func__, __LINE__, "manually setting time");

		if (nst->appsrc) {
			element = gst_object_ref(nst->appsrc);
		} else {
			fprintf(stdout, "%s:%d %s\n", __func__, __LINE__, "manually setting time: exit");
			element = NULL;
			return;
		}

		GST_OBJECT_LOCK (element);
		if (GST_ELEMENT_CLOCK (element)) {
			fprintf(stdout, "%s:%d %s\n", __func__, __LINE__, "manually setting time on pipeline");
			GstClockTime now;
			GstClockTime base_time;

			now = gst_clock_get_time (GST_ELEMENT_CLOCK (element));
			base_time = GST_ELEMENT_CAST (element)->base_time;

			basetimestamp = now - base_time;
			GST_BUFFER_TIMESTAMP (buffer) = basetimestamp;
			GST_DEBUG ("stream p: first buffer at time %" GST_TIME_FORMAT
				", base %" GST_TIME_FORMAT, GST_TIME_ARGS (now),
				GST_TIME_ARGS (base_time));
			fprintf(stdout, "first buffer at time %" GST_TIME_FORMAT
				", base %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (now),
				GST_TIME_ARGS (base_time));
		} else {

		}
			

		GST_OBJECT_UNLOCK (element);
		gst_object_unref (element);
		ret = gst_app_src_push_buffer (GST_APP_SRC_CAST (element), buffer);
	} else {
		ret = gst_app_src_push_buffer(nst->appsrc, buffer);
	}

	if (ret != GST_FLOW_OK) {
		/* something wrong, stop pushing */
        fprintf(stderr, "%s:%d %s\n", __func__, __LINE__, 
			"Something is broken in gstreamer");
	}
}

// Error handler
static neat_error_code
on_error(struct neat_flow_operations *opCB)
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }
    return 1;
}

// Abort handler
static neat_error_code
on_abort(struct neat_flow_operations *opCB)
{
    fprintf(stderr, "The flow was aborted!\n");
    exit(EXIT_FAILURE);
}

// Network change handler
static neat_error_code
on_network_changed(struct neat_flow_operations *opCB)
{
    if (config_log_level >= 1) {
        fprintf(stderr, "Something happened in the network: %d\n", (int)opCB->status);
    }

    return NEAT_OK;
}

// Timeout handler
static neat_error_code
on_timeout(struct neat_flow_operations *opCB)
{
    fprintf(stderr, "The flow reached a timeout!\n");

    exit(EXIT_FAILURE);
}

// Read data from neat
static neat_error_code
on_readable(struct neat_flow_operations *opCB)
{
	neat_error_code code;
    struct neat_streamer *nst = opCB->userData;

	code = neat_read(opCB->ctx, opCB->flow, nst->buffer, 
		nst->buffer_alloc, &nst->buffer_size, NULL, 0);

	if (code != NEAT_OK) {
		if (code == NEAT_ERROR_WOULD_BLOCK) {
			if (config_log_level >= 1) {
				printf("on_readable - NEAT_ERROR_WOULD_BLOCK\n");
			}
			return NEAT_OK;
		} else {
			fprintf(stderr, "%s - neat_read error: %d\n", __func__, (int)code);
			return on_error(opCB);
		}
	}
/* we might be able to not do this
	if(want) {
		feed_pipeline(nst);
	}
*/
	code = neat_write(opCB->ctx, proxy_flow, nst->buffer, nst->buffer_size, NULL, 0);

    return NEAT_OK;
}

// Send data from stdin
static neat_error_code
on_writable(struct neat_flow_operations *opCB)
{
	GstMapInfo map;
	static int cnt = 0;
	struct neat_streamer *nst;
	nst = opCB->userData;

	if (nst == NULL) {
		return NEAT_ERROR_OK;
	}

	/* 
	 * blocks
	 * I know, stop looking at me like that  
	 */
	g_signal_emit_by_name (nst->appsink, "pull-sample", &sample, NULL);	

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
			int code = neat_write(opCB->ctx, opCB->flow, map.data, map.size, NULL, 0);

			if (code != NEAT_OK) {
				gst_buffer_unmap (buffer, &map);
				gst_sample_unref(sample);
				exit(EXIT_FAILURE);

				return on_error(opCB);
			} else if(cnt++ > 100) {
				printf(".");
				fflush(stdout);
				cnt = 0;
			}

			gst_buffer_unmap (buffer, &map);
		}
	} else {
		g_print ("could not get frame\n");
	}

	gst_sample_unref(sample);
    return NEAT_OK;
}

static neat_error_code
on_all_written(struct neat_flow_operations *opCB)
{
    return NEAT_OK;
}

static neat_error_code
on_feedback_query(struct neat_flow_operations *opCB)
{
	if(!initiator)
		return NEAT_OK;

	if(happy) {
		return NEAT_OK;
	} else {
		return NEAT_ERROR_IO;		// TODO unhappy error :P
	}
}

static neat_error_code
on_connected(struct neat_flow_operations *opCB)
{

    struct neat_flow *flow = NULL;
    struct neat_streamer *nst = NULL;
	struct neat_flow_operations *ops = NULL;

    if (config_log_level >= 1) {
        printf("connected\n");
    }

    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

	flow = opCB->flow;

    fprintf(stderr, "%s - flow connected, port: %d\n", __func__, flow->port);
	if (flow->port == 6901 || flow->port == 6900) {
		fprintf(stderr, "%s - exiting %d\n", __func__, flow->port);
		return NEAT_ERROR_OK;
	}

	ops = calloc(1, sizeof(struct neat_flow_operations));

    if (ops == NULL) {
        fprintf(stderr, "%s - could not allocate neat_flow_operations\n", __func__);
        exit(EXIT_FAILURE);
    }
	memcpy(ops, opCB, sizeof(struct neat_flow_operations));

    if ((ops->userData = alloc_neat_streamer()) == NULL) {
        fprintf(stderr, "%s - could not allocate neat_streamer\n", __func__);
        exit(EXIT_FAILURE);
    }

    nst = ops->userData;
	nst->gst_buffer = gst_buffer_new_allocate(NULL, config_buffer_size_max, NULL);
	gst_buffer_ref(nst->gst_buffer); 	//bump the ref count, docs are not clear if new does this.

    if(camerasrc) {
        fprintf(stdout, "%s:%d %s \n", __func__, __LINE__, "Sending video");

		nst->appsink = setupvideosender();

       // ops->on_readable = NULL;
        ops->on_writable = on_writable;
        //ops->on_all_written = NULL;
        ops->on_connected = NULL;
	}

	if(displaysink) {
		if(connected) {
			fprintf(stdout, "%s:%d %s \n", __func__, __LINE__, "double connect, exiting...");
			exit(EXIT_FAILURE);
		} else {
			connected = 1;
		}

        fprintf(stdout, "%s:%d %s \n", __func__, __LINE__, "Receiving video");

		nst->appsrc = setupvideoreceiver();
		prepare_handle->data = nst;
		uv_prepare_start(prepare_handle, pump_g_loop);

        ops->on_readable = on_readable;
        //ops->on_writable = NULL;
        //ops->on_all_written = NULL;
        ops->on_connected = NULL;
    }

	if(duplex) {
        ops->on_feedback_query = on_feedback_query;
	}

    neat_set_qos(flow->ctx, flow, 0x2e);

    neat_set_operations(flow->ctx, flow, ops);
    return NEAT_OK;
}

static neat_error_code
on_close(struct neat_flow_operations *opCB)
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    fprintf(stderr, "%s - flow closed OK!\n", __func__);

    return NEAT_OK;
}

int
read_file(const char *filename, const char **bufptr)
{
    int rc;
    struct stat stat_buffer;
    char *buffer = NULL;
    FILE *f = NULL;
    size_t file_size, offset = 0;

    if ((rc = stat(filename, &stat_buffer)) < 0)
        goto error;

    assert(stat_buffer.st_size >= 0);

    file_size = (size_t)stat_buffer.st_size;

    buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        rc = -ENOMEM;
        goto error;
    }

    f = fopen(filename, "r");
    if (!f) {
        rc = -EIO;
        goto error;
    }

    do {
        size_t bytes_read = fread(buffer + offset, 1, file_size - offset, f);
        if (bytes_read < file_size - offset && ferror(f))
            goto error;
        offset += bytes_read;
    } while (offset < file_size);

    fclose(f);

    buffer[file_size] = 0;

    *bufptr = buffer;
    return 0;

	error:
		if (buffer)
			free(buffer);
		if (f)
			fclose(f);
		*bufptr = NULL;
		return rc;
}

/*
 * print usage and exit
 */
static void
print_usage()
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    printf("neat-streamer -h host [OPTIONS]\n");
    printf("\t- P \tneat properties file\n");
    printf("\t- S \tbuffer in byte (%d)\n", config_buffer_size_max);
    printf("\t- v \tlog level 0..3 (%d)\n", config_log_level);
    printf("\t- p \tport (%d)\n", config_port);
    printf("\t- c \tfile (pem cert thing)\n");
    printf("\t- r \t(receive only)\n");
    printf("\t- s \t(send only)\n");
    printf("\t- d \t(send and recv)\n");

	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
     /*
      * neat-streamer operations list:
      *      1. Set up the local video
      *          - set up a mixer with
      *              - a test source
      *              - overlay text
      *              - camera video
      *          - get it playing
      *      2. Push video to remote
      *          - pull video from camera via neat to network
      *      3. ???
      *      4. PROFIT!
	  *
	  *
	  *
	  * =======================================================
	  *   _____ ___  ___   ___  
	  *  |_   _/ _ \|   \ / _ \ 
	  *    | || (_) | |) | (_) |
	  *    |_| \___/|___/ \___/ 
	  *
	  *		Command line:
	  *			neat app sink (what we already do)
	  *			neat app src (what we need to do first)
	  *
	  *			then the above ^^^^^^
	  *
      */

	char *target_addr = NULL;
	char *arg_property = config_property;
	int result;
	int arg;

	srandom(time(NULL));

	int display_port = random() % 20000 + 5000;
	display_port = 6900;
	//int feed_port;

    result = EXIT_SUCCESS;
	gst_init (&argc, &argv);

	neat_log_level(NEAT_LOG_OFF);

	while ((arg = getopt(argc, argv, "P:S:v:h:p:c:srd")) != -1) {
        switch(arg) {
        case 'P':
/* TODO: Properties from file
		   if (read_file(optarg, &arg_property) < 0) {
						fprintf(stderr, "Unable to read properties from %s: %s",
								optarg, strerror(errno));
						result = EXIT_FAILURE;
						goto cleanup;
					}
*/
					if (config_log_level >= 1) {
						fprintf(stderr, "%s - option - properties: %s NOT READING DISABLED\n", 
							__func__, arg_property);
						fprintf(stderr, "%s - option - properties: %s\n", 
							__func__, arg_property);
					}
			break;
        case 'S':
            config_buffer_size_max = atoi(optarg);
            if (config_log_level >= 1) {
                printf("option - buffer size: %d\n", config_buffer_size_max);
            }
            break;
        case 'v':
            config_log_level = atoi(optarg);
            if (config_log_level >= 1) {
                printf("option - log level: %d\n", config_log_level);
            }
            break;
        case 'h':
            target_addr = optarg;
			initiator = 1;
            break;
        case 'p':
            config_port = atoi(optarg);
            break;
         case 'c':
             config_pem_file = optarg;
             if (config_log_level >= 1) {
                 printf("option - pem file: %s\n", config_pem_file);
             }
             break;
        case 's':
			camerasrc = 1;
			display_port++;
            break;
        case 'r':
			displaysink = 1;
            break;
        case 'd':
			camerasrc = 1;
			displaysink = 1;
            break;
        default:
            print_usage();
            goto cleanup;
            break;
        }
    }
	duplex = 1;

	//if(target_addr == NULL && !initiator) {
		//print_usage();
	//}

    if ((ctx = neat_init_ctx()) == NULL) {
        fprintf(stderr, "%s - error: could not initialize context\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }


    // new neat flow
    if ((main_flow = neat_new_flow(ctx)) == NULL) {
        fprintf(stderr, "%s - error: could not create new neat flow\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // set properties
    if (neat_set_property(ctx, main_flow, arg_property ? arg_property : config_property)) {
        fprintf(stderr, "%s:%d - error: neat_set_property\n", __func__, __LINE__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // new neat flow
    if ((proxy_flow = neat_new_flow(ctx)) == NULL) {
        fprintf(stderr, "%s - error: could not create new neat proxy flow\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // set properties
    if (neat_set_property(ctx, proxy_flow, arg_property ? arg_property : config_property)) {
        fprintf(stderr, "%s:%d - error: neat_set_property\n", __func__, __LINE__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    memset(&ops, 0, sizeof(ops));

    // set callbacks
    ops.on_connected = on_connected;
	//ops.on_writable = on_writable;	//accept flow will never be writable
    ops.on_error = on_error;
    ops.on_close = on_close;
    ops.on_aborted = on_abort;
    ops.on_network_status_changed = on_network_changed;
    ops.on_timeout = on_timeout;

    if (neat_set_operations(ctx, main_flow, &ops)) {
        fprintf(stderr, "%s - error: neat_set_operations\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    ops.on_connected = on_connected;
	//ops.on_writable = on_writable;	//accept flow will never be writable
    //ops.on_error = on_error;
    //ops.on_close = on_close;
    //ops.on_aborted = on_abort;
    //ops.on_network_status_changed = on_network_changed;
    //ops.on_timeout = on_timeout;

    if (neat_set_operations(ctx, proxy_flow, &ops)) {
        fprintf(stderr, "%s - error: neat_set_operations\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

	fprintf(stdout, "%s:%d %s %s:%d\n",
		__func__, __LINE__, "Opening proxy flow to, ", "127.0.0.1", display_port);

	if (neat_open(ctx, proxy_flow, "127.0.0.1", display_port, NULL, 0) != NEAT_OK) {
		fprintf(stderr, "%s - error: neat_open\n", __func__);
		result = EXIT_FAILURE;
		goto cleanup;
	}

	/* MAIN BIT */
	if(initiator) {
        fprintf(stdout, "%s:%d %s %s:%d\n",
			__func__, __LINE__, "Connecting to, ", target_addr, config_port);
		if (neat_open(ctx, main_flow, target_addr, 6969, NULL, 0) != NEAT_OK) {
			fprintf(stderr, "%s - error: neat_open\n", __func__);
			result = EXIT_FAILURE;
			goto cleanup;
		}
	} else {
        fprintf(stdout, "%s:%d %s %d\n",
			__func__, __LINE__, "Listening on port", config_port);
        if (neat_accept(ctx, main_flow, config_port, NULL, 0) != NEAT_OK) {
            fprintf(stderr, "%s - neat_accept failed\n", __func__);
            result = EXIT_FAILURE;
            goto cleanup;
        }
	}
	/* setup integration with g_mainloop */

	prepare_handle = calloc(1, sizeof(uv_prepare_t));
	if(prepare_handle == NULL) {
		goto cleanup;
	}
	uv_prepare_init(ctx->loop, prepare_handle);

	neat_start_event_loop(ctx, NEAT_RUN_DEFAULT); /* Blocks while app runs */

cleanup:
/* this should be somewhere
	gst_element_set_state (camerapipeline, GST_STATE_NULL);
	gst_object_unref (camerapipeline);
*/

	if(prepare_handle != NULL) {
		uv_prepare_stop(prepare_handle);
		free(prepare_handle);
	}

    if (ctx != NULL) {
        neat_free_ctx(ctx);
    }

    exit(result);
}
