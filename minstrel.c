#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <gst/gst.h>
#include <sqlite3.h>
#include <gio/gio.h>
#include <glib.h>

#include "util.h"
#include "index.h"
#include "queue.h"
#include "conn.h"

#ifdef USE_LIBNOTIFY
#include <libnotify/notify.h>
#endif

// for interaction with dbus we need an application name
#define APPNAME "minstrel"

GMainLoop *loop = NULL;
GstElement *play = NULL;
sqlite3 *player_index_db = NULL;
sqlite3_stmt *tune_select = NULL;

#ifdef USE_LIBNOTIFY
NotifyNotification *notification;
#endif

void do_notify(void) {
#ifdef USE_LIBNOTIFY
	go_to_tune(player_index_db, tune_select, queue_currently_playing());

	char *text = NULL;
	asprintf(&text, "%s from %s by %s", sqlite3_column_text(tune_select, 12), sqlite3_column_text(tune_select, 0), sqlite3_column_text(tune_select, 1));
	oomp(text);
	notify_notification_update(notification, (const char *)sqlite3_column_text(tune_select, 12), text, NULL);
	GError *error = NULL;
	if (!notify_notification_show(notification, &error)) {
		fprintf(stderr, "Error displaying notification: %s\n", error->message);
		g_error_free(error);
		return;
	}
	free(text);
#endif
}

void tunes_play(struct item *item) {
	sqlite3_stmt *get_filename = NULL;
	const unsigned char *filename = NULL;
	
	if (sqlite3_prepare_v2(player_index_db,"select filename from tunes where id = ?", -1, &get_filename, NULL) != SQLITE_OK) goto tunes_play_sqlite3_failure;
	
	if (sqlite3_bind_int64(get_filename, 1, item->id) != SQLITE_OK) goto tunes_play_sqlite3_failure;
	if (sqlite3_step(get_filename) != SQLITE_ROW) goto tunes_play_sqlite3_failure;
	
	filename = sqlite3_column_text(get_filename, 0);
	
	gst_element_set_state(play, GST_STATE_READY);
	display_queue(player_index_db, tune_select);
	
	do_notify();
	
	g_object_set(G_OBJECT(play), "uri", filename, NULL);
	gst_element_set_state(play, GST_STATE_PLAYING);
	
	sqlite3_finalize(get_filename);

	return;
	
tunes_play_sqlite3_failure:
	
	fprintf(stderr, "Sqlite3 error starting to play %ld [%s]: %s\n", item->id, filename, sqlite3_errmsg(player_index_db));
	if (get_filename != NULL) sqlite3_finalize(get_filename);
	exit(EXIT_FAILURE);
}

static void play_pause_action(void) {
	GstState state, pending;
	gst_element_get_state(play, &state, &pending, GST_SECOND);
	
	if (state == GST_STATE_PLAYING) {
		gst_element_set_state(play, GST_STATE_PAUSED);
	} else if (state == GST_STATE_PAUSED) {
		gst_element_set_state(play, GST_STATE_PLAYING);
	} else {
		tunes_play(queue_currently_playing());
	}

}

static void stop_action(void) {
	gst_element_set_state(play, GST_STATE_NULL);
	printf("\n");
}

static void next_action(void) {
	printf("\n");
	advance_queue(player_index_db);
	tunes_play(queue_currently_playing());
}

static void prev_action(void) {
	if (queue_to_prev()) {
		tunes_play(queue_currently_playing());
	}
}

static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
	switch(GST_MESSAGE_TYPE(message)) {
		case GST_MESSAGE_ERROR: {
			GError *err = NULL;
			gchar *debug;
			
			gst_message_parse_error(message, &err, &debug);
			printf("Error: %s\n", err->message);
			g_error_free(err);
			g_free(debug);
			
			g_main_loop_quit(loop);
			break;
		}
		
		case GST_MESSAGE_EOS:
			next_action();
			break;
			
		default:
			// unhandled message
			break;
	}
	
	return TRUE;
}

static gboolean cb_print_position(void *none) {
	if (play == NULL) return TRUE;
	
	GstState state, pending;
	gst_element_get_state(play, &state, &pending, GST_SECOND);
	if (state != GST_STATE_PLAYING) return TRUE;
	
	GstFormat fmt = GST_FORMAT_TIME;
	gint64 pos, len;
	if (gst_element_query_position(play, &fmt, &pos) && gst_element_query_duration(play, &fmt, &len)) {
		int64_t len_secs = len / 1000000000;
		int64_t pos_secs = pos / 1000000000;
		
		int64_t len_mins = len_secs / 60;
		len_secs %= 60;
		
		int64_t pos_mins = pos_secs / 60;
		pos_secs %= 60;
		
		printf("Position %ld:%02ld / %ld:%02ld\r", pos_mins, pos_secs, len_mins, len_secs);
		fflush(stdout);
	}
	
	return TRUE;
}

static void usage(void) {
	fprintf(stderr, "minstrel [command] [arguments]\n");
	fprintf(stderr, "commands:\n");
	//TODO: write
}

static void g_streamer_init(void) {
	int argc = 0;
	
	gst_init(&argc, NULL);
	loop = g_main_loop_new(NULL, FALSE);
}

static void g_streamer_begin(void) {
	play = gst_element_factory_make("playbin2", "play");
	
	GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(play));
	gst_bus_add_watch(bus, bus_callback, loop);
	gst_object_unref(bus);
	
	g_timeout_add(1000, (GSourceFunc)cb_print_position, NULL);
}

static void g_streamer_end() {
	gst_element_set_state(play, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(play));
}

static void dbus_signal_callback(GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data) {
	if (strcmp(signal_name, "MediaPlayerKeyPressed") != 0) return;
	if (!g_variant_is_container(parameters)) return;
	if (strcmp(g_variant_type_peek_string(g_variant_get_type(parameters)), "(ss)") != 0) return;

	GVariant *app_variant = g_variant_get_child_value(parameters, 0);
	GVariant *key_variant = g_variant_get_child_value(parameters, 1);
	
	const gchar *app = g_variant_get_string(app_variant, NULL);
	const gchar *key = g_variant_get_string(key_variant, NULL);
	
	g_variant_unref(app_variant);
	g_variant_unref(key_variant);
	
	if (strcmp(app, APPNAME) != 0) return;
	
	//printf("\nPressed key: %s\n", key);
	
	if (strcmp(key, "Play") == 0) {
		play_pause_action();
	} else if (strcmp(key, "Stop") == 0) {
		stop_action();
	} else if (strcmp(key, "Next") == 0) {
		next_action();
	} else if (strcmp(key, "Previous") == 0) {
		prev_action();
	}
}

static void dbus_register(void) {
	GError *error = NULL;
	
	GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	if (connection == NULL) {
		fprintf(stderr, "Failed to open dbus: %s\n", error->message);
		g_error_free(error);
		return;
	}
	
	GDBusProxy *proxy = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.gnome.SettingsDaemon", "/org/gnome/SettingsDaemon/MediaKeys", "org.gnome.SettingsDaemon.MediaKeys", NULL, &error);
	if (connection == NULL) {
		fprintf(stderr, "Failed to create media keys proxy: %s\n", error->message);
		g_error_free(error);
		return;
	}
	
	GVariant *result = g_dbus_proxy_call_sync(proxy, "GrabMediaPlayerKeys", g_variant_new("(su)", APPNAME, 0), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	if (result == NULL) {
		fprintf(stderr, "Failed to grab media player keys: %s\n", error->message);
		g_error_free(error);
		return;
	} else {
		g_variant_unref(result); // not interested in this result (does it even contain anything?)
	}
	
	g_signal_connect(proxy, "g-signal", G_CALLBACK(dbus_signal_callback), NULL);
}

static void start_player(void) {
	int fd = conn();
	if (fd >= 0) {
		fprintf(stderr, "There is already another instance of minstrel running\n");
		close(fd);
		exit(EXIT_FAILURE);
	}
	
	term_init();
	queue_init();
	player_index_db = open_or_create_index_db(false);
	
#ifdef USE_LIBNOTIFY
	if (!notify_init(APPNAME)) {
		fprintf(stderr, "Notify initialization failed\n");
	}
	notification = notify_notification_new("blap", "", NULL, NULL);
#endif
	
	if (sqlite3_prepare_v2(player_index_db, "select trim(album), trim(artist), trim(album_artist), trim(comment), trim(composer), trim(copyright), trim(date), trim(disc), trim(encoder), trim(genre), trim(performer), trim(publisher), trim(title), trim(track), filename from tunes where id = ?", -1, &tune_select, NULL) != SQLITE_OK) {
		fprintf(stderr, "Failed to create index access query: %s\n", sqlite3_errmsg(player_index_db));
		exit(EXIT_FAILURE);
	}

	g_streamer_init();
	g_streamer_begin();
	dbus_register();
	
	advance_queue(player_index_db);
	tunes_play(queue_currently_playing());
	
	//TODO: create the server socket and add it to the event loop
	
	g_main_loop_run(loop);
	g_streamer_end();
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		usage();
		exit(EXIT_FAILURE);
	}
	
	if (strcmp(argv[1], "index") == 0) {
		index_command(argv+2, argc-2);
	} else if (strcmp(argv[1], "start") == 0) {
		start_player();
	} else {
		fprintf(stderr, "Unknown command %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	
	return 0;
}

//TODO:
// - remote control through a unix domain socket
// - search command
// - add to queue command
// - clear queue command
// - query command
// - restrict command
// - auto-enter stop mode
