/* example appsrc for gstreamer 1.0 with own mainloop & external buffers. based on example from gstreamer docs.
 * public domain, 2015 by Florian Echtler <floe@butterbrot.org>. compile with:
 * gcc --std=c99 -Wall $(pkg-config --cflags gstreamer-1.0) -o gst gst.c $(pkg-config --libs gstreamer-1.0) -lgstapp-1.0
 * gcc --std=c99 -Wall $(pkg-config --cflags gstreamer-1.0) -o gst appsrc-loop.c $(pkg-config --libs gstreamer-1.0) -L/usr/local/lib/ -lgstapp-1.0
 */
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <stdint.h>

int want = 1;

uint16_t b_white[385*288];
uint16_t b_black[385*288];

static void prepare_buffer(GstAppSrc* appsrc) {

  static gboolean white = FALSE;
  static GstClockTime timestamp = 0;
  GstBuffer *buffer;
  guint size;
  GstFlowReturn ret;

  if (!want) return;
  want = 0;

  size = 385 * 288 * 2;

  buffer = gst_buffer_new_wrapped_full( 0, (gpointer)(white?b_white:b_black), size, 0, size, NULL, NULL );

  white = !white;

  GST_BUFFER_PTS (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 4);

  timestamp += GST_BUFFER_DURATION (buffer);

  ret = gst_app_src_push_buffer(appsrc, buffer);

  if (ret != GST_FLOW_OK) {
    /* something wrong, stop pushing */
    // g_main_loop_quit (loop);
  }
}

static void cb_need_data (GstElement *appsrc, guint unused_size, gpointer user_data) {
  //prepare_buffer((GstAppSrc*)appsrc);
  want = 1;
}

gint main (gint argc, gchar *argv[]) {

  GstElement *pipeline, *appsrc, *conv, *videosink;

  for (int i = 0; i < 385*288; i++) { b_black[i] = 0; b_white[i] = 0xFFFF; }

  /* init GStreamer */
  gst_init (&argc, &argv);

  /* setup pipeline */
  pipeline = gst_pipeline_new ("pipeline");
  appsrc = gst_element_factory_make ("appsrc", "source");
  conv = gst_element_factory_make ("videoconvert", "conv");
  videosink = gst_element_factory_make ("autovideosink", "videosink");

  /* setup */
  g_object_set (G_OBJECT (appsrc), "caps",
  		gst_caps_new_simple ("video/x-raw",
				     "format", G_TYPE_STRING, "RGB16",
				     "width", G_TYPE_INT, 384,
				     "height", G_TYPE_INT, 288,
				     "framerate", GST_TYPE_FRACTION, 0, 1,
				     NULL), NULL);
  gst_bin_add_many (GST_BIN (pipeline), appsrc, conv, videosink, NULL);
  gst_element_link_many (appsrc, conv, videosink, NULL);

  /* setup appsrc */
  g_object_set (G_OBJECT (appsrc),
		"stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
		"format", GST_FORMAT_TIME,
    "is-live", TRUE,
    NULL);
  g_signal_connect (appsrc, "need-data", G_CALLBACK (cb_need_data), NULL);

  /* play */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  
  while (1) {
    prepare_buffer((GstAppSrc*)appsrc);
    g_main_context_iteration(g_main_context_default(),FALSE);
  }

  /* clean up */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
