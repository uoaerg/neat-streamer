#include <gst/gst.h>

//#include <gst/app/gstappsink.h>

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
#include <uv.h>

#include "../neat/neat.h"
#include "../neat/neat_internal.h"

#define HOST "127.0.0.1"
#define PORT 5000

/**********************************************************************

    neat streaming app
    client [OPTIONS] HOST PORT NOT IMPLEMENTED

**********************************************************************/

static uint16_t config_log_level = 1;

static struct neat_flow_operations ops;
static struct neat_ctx *ctx = NULL;
static struct neat_flow *flow = NULL;

static char *config_property = "{\n\
    \"transport\": [\n\
        {\n\
            \"value\": \"SCTP\",\n\
            \"precedence\": 1\n\
        },\n\
        {\n\
            \"value\": \"TCP\",\n\
            \"precedence\": 1\n\
        }\n\
    ]\n\
}";


GstElement *pipeline, *sink;
GstSample *sample;

static neat_error_code on_all_written(struct neat_flow_operations *opCB);

// Print usage and exit
static void
print_usage()
{
    printf("client [OPTIONS] HOST PORT\n");
    exit(EXIT_FAILURE);
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
    return NEAT_OK;
}

// Send data from stdin
static neat_error_code
on_writable(struct neat_flow_operations *opCB)
{
	GstMapInfo map;
	static int cnt = 0;

	/* 
	 * blocks
	 * I know, stop looking at me like that  
	 */
	g_signal_emit_by_name (sink, "pull-sample", &sample, NULL);	

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
main(int argc, char *argv[])
{

	gchar *descr;
	GError *error = NULL;
	GstStateChangeReturn ret;

    char *arg_property = NULL;
	int result;

	gst_init (&argc, &argv);

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
	ret = gst_element_get_state (pipeline, NULL, NULL, 5 * GST_SECOND); //can we kill this?
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_print ("failed to play source\n");
		exit (-1);
	}

    memset(&ops, 0, sizeof(ops));

    result = EXIT_SUCCESS;

    if ((ctx = neat_init_ctx()) == NULL) {
        fprintf(stderr, "%s - error: could not initialize context\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // new neat flow
    if ((flow = neat_new_flow(ctx)) == NULL) {
        fprintf(stderr, "%s - error: could not create new neat flow\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // set properties
    if (neat_set_property(ctx, flow, arg_property ? arg_property : config_property)) {
        fprintf(stderr, "%s - error: neat_set_property\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // set callbacks
    ops.on_connected = on_connected;
	ops.on_writable = on_writable;
    ops.on_error = on_error;
    ops.on_close = on_close;
    ops.on_aborted = on_abort;
    ops.on_network_status_changed = on_network_changed;
    ops.on_timeout = on_timeout;

    if (neat_set_operations(ctx, flow, &ops)) {
        fprintf(stderr, "%s - error: neat_set_operations\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // wait for on_connected or on_error to be invoked
    if (neat_open(ctx, flow, HOST, PORT, NULL, 0) == NEAT_OK) {
        neat_start_event_loop(ctx, NEAT_RUN_DEFAULT);
    } else {
        fprintf(stderr, "%s - error: neat_open\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

cleanup:

    // cleanup
    if (flow != NULL) {
        neat_free_flow(flow);
    }
    if (ctx != NULL) {
        neat_free_ctx(ctx);
    }

	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_object_unref (pipeline);

    exit(result);
}
