#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "../neat/neat.h"
#include "../neat/neat_internal.h"

/**********************************************************************

    neat streaming app
    client [OPTIONS] HOST PORT NOT IMPLEMENTED

**********************************************************************/

static uint16_t config_log_level = 1;

static struct neat_flow_operations ops;
static struct neat_ctx *ctx = NULL;
static struct neat_flow *flow = NULL;

static neat_error_code on_all_written(struct neat_flow_operations *opCB);

#define HOST "127.0.0.1"
#define PORT 5000

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
    uint64_t prop = 0;
	int result;

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

	prop |= NEAT_PROPERTY_UDP_REQUIRED | NEAT_PROPERTY_IPV4_REQUIRED;

    // set properties
    if (neat_set_property(ctx, flow, prop)) {
        fprintf(stderr, "%s - error: neat_set_property\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // set callbacks
    ops.on_connected = on_connected;
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
    if (neat_open(ctx, flow, HOST, PORT) == NEAT_OK) {
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
    exit(result);
}
