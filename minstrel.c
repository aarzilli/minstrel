#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <gst/gst.h>
#include <sqlite3.h>

#include "util.h"
#include "index.h"

GMainLoop *loop = NULL;

static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
	switch(GST_MESSAGE_TYPE(message)) {
		case GST_MESSAGE_ERROR: {
			GError *err;
			gchar *debug;
			
			gst_message_parse_error(message, &err, &debug);
			printf("Error: %s\n", err->message);
			g_error_free(err);
			g_free(debug);
			
			g_main_loop_quit(loop);
			break;
		}
		
		case GST_MESSAGE_EOS:
			printf("\n");
			//TODO: play next item from queue?
			g_main_loop_quit(loop);
			break;
			
		default:
			// unhandled message
			break;
	}
	
	return TRUE;
}

static gboolean cb_print_position(GstElement *pipeline) {
	GstFormat fmt = GST_FORMAT_TIME;
	gint64 pos, len;
	if (gst_element_query_position(pipeline, &fmt, &pos) && gst_element_query_duration(pipeline, &fmt, &len)) {
		g_print("Time: %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r", GST_TIME_ARGS(pos), GST_TIME_ARGS(len));
	}
	
	return TRUE;
}

static void usage(void) {
	fprintf(stderr, "minstrel [command] [arguments]\n");
	fprintf(stderr, "commands:\n");
	//TODO: write
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		usage();
		exit(EXIT_FAILURE);
	}
	
	if (strcmp(argv[1], "index") == 0) {
		index_command(argv+2, argc-2);
	} else {
		fprintf(stderr, "Unknown command %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	
#ifdef TOY_COMMENTED
	gst_init (&argc, &argv);
	loop = g_main_loop_new(NULL, FALSE);
	
	if (argc != 2) {
		printf("Usage: %s <URI>\n", argv[0]);
		return -1;
	}
	
	GstElement *play = gst_element_factory_make("playbin", "play");
	g_object_set (G_OBJECT(play), "uri", argv[1], NULL);
	
	GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(play));
	gst_bus_add_watch (bus, bus_callback, loop);
	gst_object_unref(bus);
	
	gst_element_set_state(play, GST_STATE_PLAYING);
	
	g_timeout_add(1000, (GSourceFunc)cb_print_position, play);
	
	g_main_loop_run(loop);
	
	gst_element_set_state(play, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(play));
#endif

	return 0;
}

//TODO:
// - random selection queue
// - remote control through dbus
// - libnotify interface
// - remote control through a unix domain socket
// - add to queue command
// - clear queue command
// - search command
// - query command
// - restrict command
