#include <stdlib.h>
#include <gst/gst.h>
#include <tag_c.h>

int main(int argc, char *argv[]) {
	/* init GStreamer */
	gst_init (&argc, &argv);
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	
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
	
	exit(EXIT_SUCCESS);
	
	/* set up gstreamer */
	GstElement *play = gst_element_factory_make("playbin", "play");
	g_object_set (G_OBJECT(play), "uri", argv[1], NULL);
	
	/*bus = gst_pipeline_get_bus(GST_PIPELINE(play));
	gst_bus_add_watch (bus, my_bus_callback, loop);
	gst_object_unref(bus);*/
	
	gst_element_set_state(play, GST_STATE_PLAYING);
	
	/* now run */
	g_main_loop_run(loop);
	
	/* also clean up */
	gst_element_set_state(play, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT (play));
	
	return 0;
}

//TODO:
// - Display mp3 informations
// - Display progress
// - indexing a library
// - random selection queue
// - remote control through a unix domain socket
// - remote control through dbus
// - libnotify interface
