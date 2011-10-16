#include <stdlib.h>
#include <gst/gst.h>
#include <tag_c.h>

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

int main(int argc, char *argv[]) {
	/* init GStreamer */
	gst_init (&argc, &argv);
	loop = g_main_loop_new(NULL, FALSE);
	
	/* make sure we have a URI */
	if (argc != 2) {
		printf("Usage: %s <URI>\n", argv[0]);
		return -1;
	}
	
	TagLib_File *file = taglib_file_new(argv[1]+6);
	TagLib_Tag *tag = taglib_file_tag(file);
	const TagLib_AudioProperties *properties = taglib_file_audioproperties(file);
	
	if(tag != NULL) {
		printf("-- TAG --\n");
		printf("title   - \"%s\"\n", taglib_tag_title(tag));
		printf("artist  - \"%s\"\n", taglib_tag_artist(tag));
		printf("album   - \"%s\"\n", taglib_tag_album(tag));
		printf("year    - \"%i\"\n", taglib_tag_year(tag));
		printf("comment - \"%s\"\n", taglib_tag_comment(tag));
		printf("track   - \"%i\"\n", taglib_tag_track(tag));
		printf("genre   - \"%s\"\n", taglib_tag_genre(tag));
	}
	
	if(properties != NULL) {
		int seconds = taglib_audioproperties_length(properties) % 60;
		int minutes = (taglib_audioproperties_length(properties) - seconds) / 60;
		
		printf("-- AUDIO --\n");
		printf("bitrate     - %i\n", taglib_audioproperties_bitrate(properties));
		printf("sample rate - %i\n", taglib_audioproperties_samplerate(properties));
		printf("channels    - %i\n", taglib_audioproperties_channels(properties));
		printf("length      - %i:%02i\n", minutes, seconds);
	}
	
	taglib_tag_free_strings();
	taglib_file_free(file);
	
	/* set up gstreamer */
	GstElement *play = gst_element_factory_make("playbin", "play");
	g_object_set (G_OBJECT(play), "uri", argv[1], NULL);
	
	GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(play));
	gst_bus_add_watch (bus, bus_callback, loop);
	gst_object_unref(bus);
	
	gst_element_set_state(play, GST_STATE_PLAYING);
	
	g_timeout_add(1000, (GSourceFunc)cb_print_position, play);
	
	g_main_loop_run(loop);
	
	/* also clean up */
	gst_element_set_state(play, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(play));
	
	return 0;
}

//TODO:
// - indexing a library
// - random selection queue
// - remote control through a unix domain socket
// - remote control through dbus
// - libnotify interface
