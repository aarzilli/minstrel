#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <gst/gst.h>
#include <sqlite3.h>
#include <gio/gio.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termcap.h>

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

GIOChannel *serve_channel = NULL;
guint serve_channel_source_id;

#ifdef USE_LIBNOTIFY
NotifyNotification *notification;
#endif

void do_notify(sqlite3_stmt *tune_select) {
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

bool tunes_play(struct item *item) {
	sqlite3_stmt *get_filename = NULL;
	const unsigned char *filename = NULL;


	if (sqlite3_prepare_v2(player_index_db,"select filename from tunes where id = ?", -1, &get_filename, NULL) != SQLITE_OK) goto tunes_play_sqlite3_failure;

	if (sqlite3_bind_int64(get_filename, 1, item->id) != SQLITE_OK) goto tunes_play_sqlite3_failure;

	if (sqlite3_step(get_filename) != SQLITE_ROW) {
		sqlite3_finalize(get_filename);
		return false;
	}

	filename = sqlite3_column_text(get_filename, 0);

	gst_element_set_state(play, GST_STATE_READY);

	g_object_set(G_OBJECT(play), "uri", filename, NULL);
	gst_element_set_state(play, GST_STATE_PLAYING);

	sqlite3_finalize(get_filename);

	sqlite3_stmt *tune_select;
	if (sqlite3_prepare_v2(player_index_db, "select trim(album), trim(artist), trim(album_artist), trim(comment), trim(composer), trim(copyright), trim(date), trim(disc), trim(encoder), trim(genre), trim(performer), trim(publisher), trim(title), trim(track), filename from tunes where id = ?", -1, &tune_select, NULL) != SQLITE_OK) goto tunes_play_sqlite3_failure;

	display_queue(player_index_db, tune_select);

	do_notify(tune_select);

	sqlite3_finalize(tune_select);

	return true;

tunes_play_sqlite3_failure:

	fprintf(stderr, "Sqlite3 error starting to play %" PRId64 " [%s]: %s\n", item->id, filename, sqlite3_errmsg(player_index_db));
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
	for(;;) {
		advance_queue(player_index_db);
		if (tunes_play(queue_currently_playing())) break;
	}
}

static void prev_action(void) {
	bool rewind = false;
	GstState state, pending;
	gst_element_get_state(play, &state, &pending, GST_SECOND);
	if (state == GST_STATE_PLAYING) {
		gint64 pos;
		GstFormat fmt = GST_FORMAT_TIME;
		if (gst_element_query_position(play, &fmt, &pos)) {
			int64_t pos_secs = pos / 1000000000;
			if (pos_secs > 5) rewind = true;
		}
	}

	if (rewind) {
		tunes_play(queue_currently_playing());
	} else {
		if (queue_to_prev()) {
			tunes_play(queue_currently_playing());
		}
	}
}

static void rewind_action(void) {
	printf("\n");
	tunes_play(queue_currently_playing());
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

		printf("\rPosition %" PRId64 ":%02" PRId64 " / %" PRId64 ":%02" PRId64 "      ", pos_mins, pos_secs, len_mins, len_secs);
		fflush(stdout);
	}

	return TRUE;
}

static void usage(void) {
	fprintf(stderr, "minstrel [command] [arguments]\n");
	fprintf(stderr, "commands:\n");
	fprintf(stderr, "  start\t\tStart server instance\n");
	fprintf(stderr, "  play\t\tRequests server to toggle between play and pause\n");
	fprintf(stderr, "  next\t\tRequests server next track\n");
	fprintf(stderr, "  prev\t\tRequests server previous track\n");
	fprintf(stderr, "  rewind\t\tRestart current song\n");
	fprintf(stderr, "  add <id1...>\tAdds songs to queue -- list song IDs on command line or on standard input (one per line)\n");
	fprintf(stderr, "  search <query> Search for songs by full text matching of a query, output can be piped into add\n");
	fprintf(stderr, "  where <expr>\tSearch for songs with a boolean query\n");
	fprintf(stderr, "  addlast\tAdds results of last search to queue\n");
	fprintf(stderr, "  help\t\tThis message\n");
}

static void g_streamer_init(void) {
	int argc = 0;

	gst_init(&argc, NULL);
	loop = g_main_loop_new(NULL, FALSE);
}

static void g_streamer_begin(void) {
	play = gst_element_factory_make("playbin2", "play");
#ifdef SLOW_LATENCY
	gst_pipeline_set_delay(GST_PIPELINE(play), 2*GST_SECOND);
#endif

	//printf("Default latency: %d\n", gst_pipeline_get_delay(GST_PIPELINE(play)));

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

static gboolean server_watch(GIOChannel *source, GIOCondition condition, void *ignored) {
	int64_t command[2] = { 0, 0 };
	struct sockaddr_un src_addr;
	socklen_t addrlen = sizeof(src_addr);

	size_t bytes_read = recvfrom(g_io_channel_unix_get_fd(source), (void *)command, sizeof(command), 0, &src_addr, &addrlen);

	if (bytes_read != sizeof(command)) return TRUE;

	//printf("\nControl interface: %zd [ %" PRId64 " %" PRId64 " ]\n", bytes_read, command[0], command[1]);

	switch (command[0]) {
	case CMD_HANDSHAKE: {
		// Nothing to do with this
		break;
	}
	case CMD_PLAY_PAUSE:
		play_pause_action();
		break;
	case CMD_STOP:
		stop_action();
		break;
	case CMD_NEXT:
		next_action();
		break;
	case CMD_REWIND:
		rewind_action();
		break;
	case CMD_PREV:
		prev_action();
		break;
	case CMD_ADD: {
		queue_append(command[1]);

		sqlite3_stmt *tune_select;
		if (sqlite3_prepare_v2(player_index_db, "select trim(album), trim(artist), trim(album_artist), trim(comment), trim(composer), trim(copyright), trim(date), trim(disc), trim(encoder), trim(genre), trim(performer), trim(publisher), trim(title), trim(track), filename from tunes where id = ?", -1, &tune_select, NULL) != SQLITE_OK) {
			fprintf(stderr, "Sqlite3 error adding to queue: %s\n", sqlite3_errmsg(player_index_db));
		} else {
			display_queue(player_index_db, tune_select);
			sqlite3_finalize(tune_select);
		}

		break;
	}

	default:
		printf("Received unknown command: %" PRId64 "\n", command[0]);
	}

	return TRUE;
}

static void start_player(void) {
	{
		int fd = conn();
		if (fd >= 0) {
			fprintf(stderr, "There is already another instance of minstrel running\n");
			close(fd);
			exit(EXIT_FAILURE);
		}
	}

	term_init();
	queue_init();
	player_index_db = open_or_create_index_db(false);

#ifdef USE_LIBNOTIFY
	if (!notify_init(APPNAME)) {
		fprintf(stderr, "Notify initialization failed\n");
	}
	notification = notify_notification_new("blap", "", NULL);
#endif

	g_streamer_init();
	g_streamer_begin();
	dbus_register();

	advance_queue(player_index_db);
	tunes_play(queue_currently_playing());

	int fd = serve();
	serve_channel = g_io_channel_unix_new(fd);
	serve_channel_source_id = g_io_add_watch(serve_channel, G_IO_IN|G_IO_ERR|G_IO_PRI|G_IO_HUP|G_IO_NVAL, (GIOFunc)server_watch, NULL);

	g_main_loop_run(loop);
	g_streamer_end();

	close(fd);
	sqlite3_close(player_index_db);
}

static void show_search_results(sqlite3_stmt *search_select) {
	int64_t cs = 0;
	char null_str[] = "(null)";

	while (sqlite3_step(search_select) == SQLITE_ROW) {
		const char *cur_album = (const char *)sqlite3_column_text(search_select, 0);
		const char *cur_artist = (const char *)sqlite3_column_text(search_select, 1);

		if (!cur_album) cur_album = null_str;
		if (!cur_artist) cur_artist = null_str;

		int64_t cur_cs = checksum(cur_album) + checksum(cur_artist);

		if (cur_cs != cs) {
			fputs("\nFrom ", stdout);
			putctlcod("md", stdout);
			fputs(cur_album, stdout);
			putctlcod("me", stdout);
			fputs(" by ", stdout);
			putctlcod("md", stdout);
			fputs(cur_artist, stdout);
			putctlcod("me", stdout);
			fputs("\n", stdout);

			cs = cur_cs;
		}

		printf("%" PRId64 "\t%2d. ", (int64_t)sqlite3_column_int64(search_select, 15), sqlite3_column_int(search_select, 13));
		fputs(tgetstr("md", NULL), stdout);
		fputs((const char *)sqlite3_column_text(search_select, 12), stdout);
		fputs(tgetstr("me", NULL), stdout);
		fputs("\n", stdout);
	}
}

static void search_command(char *terms[], int n) {
	int size = 1;

	for (int i = 0; i < n; ++i) {
		size += 1 + strlen(terms[i]);
	}

	char *query = malloc(sizeof(char) * size);
	oomp(query);
	query[0] = '\0';

	for (int i = 0; i < n; ++i) {
		strcat(query, terms[i]);
		strcat(query, " ");
	}

	term_init();
	player_index_db = open_or_create_index_db(false);

	sqlite3_stmt *search_select;
	if (sqlite3_prepare_v2(player_index_db, "select trim(album), trim(artist), trim(album_artist), trim(comment), trim(composer), trim(copyright), trim(date), trim(disc), trim(encoder), trim(genre), trim(performer), trim(publisher), trim(title), trim(track), filename, tunes.id from tunes, ridx where tunes.id = ridx.id and any match ? order by artist, album, cast(track as integer) asc", -1, &search_select, NULL) != SQLITE_OK) goto search_sqlite3_failure;

	if (sqlite3_bind_text(search_select, 1, query, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto search_sqlite3_failure;

	show_search_results(search_select);

	sqlite3_finalize(search_select);

	char *errmsg = NULL;
	sqlite3_exec(player_index_db, "DELETE FROM search_save;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto search_sqlite3_failure;

	sqlite3_stmt *search_save;
	if (sqlite3_prepare_v2(player_index_db, "INSERT INTO search_save(id) SELECT tunes.id FROM tunes, ridx WHERE tunes.id = ridx.id AND any MATCH ? ORDER BY artist, album, cast(track as integer) ASC;", -1, &search_save, NULL) != SQLITE_OK) goto search_sqlite3_failure;

	if (sqlite3_bind_text(search_save, 1, query, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto search_sqlite3_failure;

	if (sqlite3_step(search_save) != SQLITE_DONE) goto search_sqlite3_failure;

	sqlite3_finalize(search_save);

	sqlite3_close(player_index_db);
	free(query);

	return;

search_sqlite3_failure:

	fprintf(stderr, "Sqlite3 error while searching: %s\n", sqlite3_errmsg(player_index_db));
	sqlite3_close(player_index_db);
	exit(EXIT_FAILURE);
}

static void where_command(const char *clause) {
	term_init();
	player_index_db = open_or_create_index_db(false);

	char *query;

	if (clause != NULL) {
		asprintf(&query, "select trim(album), trim(artist), trim(album_artist), trim(comment), trim(composer), trim(copyright), trim(date), trim(disc), trim(encoder), trim(genre), trim(performer), trim(publisher), trim(title), trim(track), filename, tunes.id from tunes, ridx where tunes.id = ridx.id and (%s) order by artist, album, cast(track as integer) asc", clause);
	} else {
		asprintf(&query, "select trim(album), trim(artist), trim(album_artist), trim(comment), trim(composer), trim(copyright), trim(date), trim(disc), trim(encoder), trim(genre), trim(performer), trim(publisher), trim(title), trim(track), filename, tunes.id from tunes, ridx where tunes.id = ridx.id order by artist, album, cast(track as integer) asc");
	}
	oomp(query);

	sqlite3_stmt *search_select;
	if (sqlite3_prepare_v2(player_index_db, query, -1, &search_select, NULL) != SQLITE_OK) goto where_sqlite3_failure;

	show_search_results(search_select);

	sqlite3_finalize(search_select);
	free(query);
	sqlite3_close(player_index_db);

	return;

where_sqlite3_failure:

	fprintf(stderr, "Sqlite3 error: %s\n", sqlite3_errmsg(player_index_db));
	sqlite3_close(player_index_db);
	free(query);
	return;
}

static void add_command(int argc, char *argv[]) {
	int fd = conn();
	if (fd == -1) {
		fprintf(stderr, "Couldn't connect to server\n");
		exit(EXIT_FAILURE);
	}

	if (argc > 0) {
		for (int i = 0; i < argc; ++i) {
			send_add(fd, (int64_t)atoll(argv[i]));
		}
	} else {
#define LINEBUF 40
		char buf[LINEBUF];
		int i = 0;
		int r;

		while ((r = getchar()) != EOF) {
			if (i < LINEBUF-1)
				buf[i++] = r;
			if (r == '\n') {
				buf[i] = '\0';
				if (isdigit(buf[0])) {
					char *tab = strchr(buf, '\t');
					if (tab != NULL) {
						*tab = '\0';
						send_add(fd, (int64_t)atoll(buf));
					}
				}
				i = 0;
			}
		}
	}
	close(fd);
}

static void addlast_command(void) {
	int fd = conn();
	if (fd == -1) {
		fprintf(stderr, "Couldn't connect to server\n");
		exit(EXIT_FAILURE);
	}

	player_index_db = open_or_create_index_db(false);

	sqlite3_stmt *select;
	if (sqlite3_prepare_v2(player_index_db, "SELECT id FROM search_save ORDER BY counter;", -1, &select, NULL) != SQLITE_OK) goto addlast_command_sqlite3_failure;

	while (sqlite3_step(select) == SQLITE_ROW) {
		send_add(fd, (int64_t)sqlite3_column_int64(select, 0));
	}

	sqlite3_finalize(select);
	sqlite3_close(player_index_db);
	close(fd);

	return;

addlast_command_sqlite3_failure:

	fprintf(stderr, "Sqlite error: %s\n", sqlite3_errmsg(player_index_db));
	sqlite3_close(player_index_db);
	close(fd);
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
	} else if (strcmp(argv[1], "play") == 0) {
		int64_t cmd[] = { CMD_PLAY_PAUSE, 0 };
		conn_and_send(cmd);
	} else if (strcmp(argv[1], "stop") == 0) {
		int64_t cmd[] = { CMD_STOP, 0 };
		conn_and_send(cmd);
	} else if (strcmp(argv[1], "next") == 0) {
		int64_t cmd[] = { CMD_NEXT, 0 };
		conn_and_send(cmd);
	} else if (strcmp(argv[1], "rewind") == 0) {
		int64_t cmd[] = { CMD_REWIND, 0 };
		conn_and_send(cmd);
	} else if (strcmp(argv[1], "prev") == 0) {
		int64_t cmd[] = { CMD_PREV, 0 };
		conn_and_send(cmd);
	} else if (strcmp(argv[1], "add") == 0) {
		add_command(argc-2, argv+2);
	} else if (strcmp(argv[1], "search") == 0) {
		search_command(argv+2, argc-2);
	} else if (strcmp(argv[1], "where") == 0) {
		if (argc == 2) {
			where_command(NULL);
		} else if (argc == 3) {
			where_command(argv[2]);
		} else {
			fprintf(stderr, "Wrong number of arguments to 'where'\n");
			exit(EXIT_FAILURE);
		}
	} else if (strcmp(argv[1], "addlast") == 0) {
		addlast_command();
	} else if (strcmp(argv[1], "help") == 0) {
		usage();
	} else {
		fprintf(stderr, "Unknown command %s\n", argv[1]);
		usage();
		exit(EXIT_FAILURE);
	}

	return 0;
}
