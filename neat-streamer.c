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
static int want = 0;
static int connected = 0;

uv_prepare_t *prepare_handle;

static char *config_property = "{\
    \"transport\": [\
        {\
            \"value\": \"UDP\",\
            \"precedence\": 1\
        }\
    ]\
}";\

static uint32_t config_port=6969;
static uint16_t config_log_level = 0;
static uint32_t config_buffer_size_max = 1400;
static char *config_pem_file = NULL;

static uint8_t camerasrc = 0;
static uint8_t displaysink = 0;

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

	descr =
		g_strdup_printf("videotestsrc ! x264enc ! rtph264pay ! "
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
//	struct neat_streamer *nst = handle->data;

//	fprintf(stdout, "%s:%d\n", __func__, __LINE__);
//	feed_pipeline(nst->appsrc, nst);
	g_main_context_iteration(g_main_context_default(),FALSE);
}

static void 
cb_need_data(GstElement *appsrc, guint unused_size, gpointer user_data) 
{
    if (config_log_level >= 1) {
		fprintf(stdout, "%s:%d\n", __func__, __LINE__);
    }
	//prepare_buffer((GstAppSrc*)appsrc);
	want++;
}

GstAppSrc *
setupvideoreceiver()
{
	fprintf(stdout, "%s:%d\n", __func__, __LINE__);
	GstElement *pipeline, *appsrc,  *rtpdepay, *decodebin, *videosink;

	/* setup pipeline */
	pipeline = gst_pipeline_new("pipeline");
	appsrc = gst_element_factory_make("appsrc", "source");
	rtpdepay = gst_element_factory_make("rtph264depay", "rtpdepay");
	decodebin = gst_element_factory_make("decodebin", "decodebin");
	videosink = gst_element_factory_make("autovideosink", "videosink");

	GstCaps *caps = gst_caps_from_string(
		"application/x-rtp,media=(string)video,"
		"clock-rate=(int)90000,encoding-name=(string)H264");
	g_object_set (appsrc, "caps", caps, NULL);

	gst_bin_add_many(GST_BIN(pipeline), appsrc, rtpdepay, decodebin, videosink, NULL);
	gst_element_link_many(appsrc, rtpdepay, decodebin, videosink, NULL);

	/* setup appsrc */
	g_object_set (G_OBJECT (appsrc),
		"stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
		"format", GST_FORMAT_TIME,
		"is-live", TRUE,
		NULL);
	g_signal_connect (appsrc, "need-data", G_CALLBACK (cb_need_data), NULL);

	/* play */
	fprintf(stdout, "%s:%d Setting play on pipeline\n", __func__, __LINE__);
	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	GST_DEBUG_BIN_TO_DOT_FILE((GstBin *)pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

	return (GstAppSrc *)appsrc;
}

static void
feed_pipeline(struct neat_streamer *nst)
{
    if (config_log_level >= 1) {
		fprintf(stdout, "%s:%d\n", __func__, __LINE__);
    }
	fprintf(stdout, "%s:%d data level %" G_GUINT64_FORMAT "\n", __func__, __LINE__,
		gst_app_src_get_current_level_bytes(nst->appsrc));

	static GstClockTime timestamp = 0;
	GstFlowReturn ret;

	if(!want) {
		return;
	}
	want = 0;
	if(want > 1) {
		fprintf(stdout, "%s:%d %d\n", __func__, __LINE__, want);
	}

	GstBuffer *buffer = gst_buffer_new_allocate(NULL, config_buffer_size_max, NULL);

	gst_buffer_fill(buffer, 0, nst->buffer, nst->buffer_size);

	GST_BUFFER_PTS(buffer) = timestamp;
	GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 2);
	timestamp += GST_BUFFER_DURATION(buffer);

	ret = gst_app_src_push_buffer(nst->appsrc, buffer);

	if (ret != GST_FLOW_OK) {
		/* something wrong, stop pushing */
        fprintf(stderr, "%s:%d %s\n", __func__, __LINE__, 
			"Something is broken in gstreamer");
		//exit(-1);
	}

	fprintf(stdout, "%s:%d data level %" G_GUINT64_FORMAT "\n", __func__, __LINE__,
		gst_app_src_get_current_level_bytes(nst->appsrc));
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

	if(want) {
		feed_pipeline(nst);
	}

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
				fprintf(stderr, "%s - neat_write - error: %d\n", __func__, (int)code);
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
on_connected(struct neat_flow_operations *opCB)
{
    fprintf(stderr, "%s - flow connected\n", __func__);

    struct neat_streamer *nst = NULL;

    if (config_log_level >= 1) {
        printf("connected\n");
    }

    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    if ((opCB->userData = alloc_neat_streamer()) == NULL) {
        fprintf(stderr, "%s - could not allocate neat_streamer\n", __func__);
        exit(EXIT_FAILURE);
    }

    //neat_set_qos(opCB->ctx, opCB->flow, 0x2e);
    //neat_set_ecn(opCB->ctx, opCB->flow, 0x00);

    nst = opCB->userData;
	nst->gst_buffer = gst_buffer_new_allocate(NULL, config_buffer_size_max, NULL);
	gst_buffer_ref(nst->gst_buffer); 	//bump the ref count, docs are not clear if new does this.

    if(camerasrc) {
        fprintf(stdout, "%s:%d %s \n", __func__, __LINE__, "Sending video");

		nst->appsink = setupvideosender();

        opCB->on_readable = NULL;
        opCB->on_writable = on_writable;
        opCB->on_all_written = NULL;
        opCB->on_connected = NULL;
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

        opCB->on_readable = on_readable;
        opCB->on_writable = NULL;
        opCB->on_all_written = NULL;
        opCB->on_connected = NULL;
    }

    neat_set_operations(opCB->ctx, opCB->flow, opCB);
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
    printf("\t- F \tfire video at [host:port]\n");
    printf("\t- r \t(receive only)\n");
    printf("\t- s \t(send only)\n");

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

    result = EXIT_SUCCESS;
	gst_init (&argc, &argv);

	neat_log_level(NEAT_LOG_OFF);

	while ((arg = getopt(argc, argv, "P:S:v:h:p:c:sr")) != -1) {
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
            break;
        case 'r':
			displaysink = 1;
            break;
        default:
            print_usage();
            goto cleanup;
            break;
        }
    }

	if(target_addr == NULL) {
		print_usage();
	}


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
        fprintf(stderr, "%s - error: neat_set_property\n", __func__);
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

	/* MAIN BIT */
	if(camerasrc) {
        fprintf(stdout, "%s:%d %s %s:%d\n",
			__func__, __LINE__, "Connecting to, ", target_addr, config_port);
		if (neat_open(ctx, main_flow, target_addr, config_port, NULL, 0) != NEAT_OK) {
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
