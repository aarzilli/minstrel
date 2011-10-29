#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include <gst/gst.h>
#include <tag_c.h>
#include <sqlite3.h>
#include <sys/types.h>
#include <dirent.h>
#include <magic.h>

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

static void oomp(void *ptr) {
	if (ptr == NULL) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
}

static void usage(void) {
	fprintf(stderr, "minstrel [command] [arguments]\n");
	fprintf(stderr, "commands:\n");
	//TODO: write
}

static bool sqlite3_has_table(sqlite3 *db, const char *name) {
	sqlite3_stmt *statement = NULL;
	int r;
	
	r = sqlite3_prepare_v2(db, "select name from sqlite_master where name = ?", -1, &statement, NULL);
	if (r != SQLITE_OK) goto sqlite3_has_table_failure;
	
	r = sqlite3_bind_text(statement, 1, name, -1, SQLITE_TRANSIENT);
	if (r != SQLITE_OK) goto sqlite3_has_table_failure;
	
	r = sqlite3_step(statement);
	bool ret = (r == SQLITE_ROW);
	sqlite3_finalize(statement);
	return ret;
	
sqlite3_has_table_failure:

	fprintf(stderr, "Error preparing has_table statement: %s\n", sqlite3_errmsg(db));
	sqlite3_finalize(statement);
	if (statement == NULL) sqlite3_finalize(statement);
	exit(EXIT_FAILURE);
}

static sqlite3 *open_or_create_index_db(void) {
	char *errmsg;
	int r;
	sqlite3 *index_db;
	
	{
		char *index_file_name;
		asprintf(&index_file_name, "%s/.index", getenv("HOME"));
		oomp(index_file_name);
		
		r = sqlite3_open_v2(index_file_name, &index_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);

		oomp(index_db);
		
		if (r != SQLITE_OK) {
			fprintf(stderr, "Failed to open/create index file: %s\n", sqlite3_errmsg(index_db));
			exit(EXIT_FAILURE);
		}
		
		free(index_file_name);
	}
	
	sqlite3_exec(index_db, "pragma foreign_keys = on;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;
	
	sqlite3_exec(index_db, "pragma synchronous = off;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;
	
	sqlite3_exec(index_db, "CREATE TABLE IF NOT EXISTS config(key text, value text);", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;
	
	//TODO: initialize configuration
	
	sqlite3_exec(index_db, "CREATE TABLE IF NOT EXISTS tunes(id integer primary key autoincrement, title text, artist test, album text, year integer, comment text, track integer, genre text);", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;

	if (!sqlite3_has_table(index_db, "ridx")) {
		sqlite3_exec(index_db, "CREATE VIRTUAL TABLE ridx USING fts3(id integer, any text, foreign key (id) references tunes(id) on delete cascade deferrable initially deferred);", NULL, NULL, &errmsg);
		if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;
	}
	
	sqlite3_exec(index_db, "DELETE FROM tunes;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;
	
	sqlite3_exec(index_db, "DELETE FROM ridx;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;

	return index_db;
	
open_or_create_index_db_sqlite3_failure:

	fprintf(stderr, "Sqlite error building index: %s\n", errmsg);
	sqlite3_free(errmsg);
	sqlite3_close(index_db);
	exit(EXIT_FAILURE);
}

static bool strstart(const char *haystack, const char *needle) {
    return strncmp(needle, haystack, strlen(needle)) == 0;
}

const char *KNOWN_AUDIO_EXTENSIONS[] = { "aif", "aiff", "m4a", "mid", "mp3", "mpa", "ra", "wav", "wma", "aac", "mp4", "m4p", "m4r", "3gp", "ogg", "oga", "au", "3ga", "aifc", "aifr", "alac", "caf", "caff" };

static bool should_autoindex_file(const char *full_file_name, magic_t magic_cookie) {
	const char *magic_type = magic_file(magic_cookie, full_file_name);
	if (strstart(magic_type, "audio")) return true;
	if (strcmp(magic_type, "application/ogg") == 0) return true;
	
	//printf("Discarded by audio check %s\n", magic_type);
	if (strcmp(magic_type, "application/octet-stream") == 0) {
		//printf("Checking extension\n");
		char *dot = strrchr(full_file_name, '.');
		if (dot != NULL) {
			char *ext = strdup(dot+1);
			oomp(ext);
			
			for (char *c = ext; *c != '\0'; ++c) {
				*c = tolower(*c);
			}
			
			//printf("Checking extension: %s\n", ext);
			
			bool r = false;
			
			for (int i = 0; i < sizeof(KNOWN_AUDIO_EXTENSIONS)/sizeof(const char *); ++i) {
				if (strcmp(KNOWN_AUDIO_EXTENSIONS[i], ext) == 0) {
					r = true;
					break;
				}
			}
			
			free(ext);
			return r;
		}
	}
	
	return false;
}

static void index_file(sqlite3 *index_db, const char *filename) {
}

static void index_directory(sqlite3 *index_db, char *dir_name, magic_t magic_cookie) {
	DIR *dir = opendir(dir_name);
	if (dir == NULL) {
		fprintf(stderr, "Can not index %s, can not open directory\n", dir_name);
		return;
	}
	
	for (struct dirent *curent = readdir(dir); curent != NULL; curent = readdir(dir)) {
		if (curent->d_name[0] == '.') continue;
		if (curent->d_type == DT_DIR) {
			char *new_dir_name;
			asprintf(&new_dir_name, "%s/%s", dir_name, curent->d_name);
			oomp(new_dir_name);
			index_directory(index_db, new_dir_name, magic_cookie);
			free(new_dir_name);
		} else if (curent->d_type == DT_REG) {
			char *full_file_name;
			
			asprintf(&full_file_name, "%s/%s", dir_name, curent->d_name);
			oomp(full_file_name);
			
			if (should_autoindex_file(full_file_name, magic_cookie)) {
				index_file(index_db, full_file_name);
			} else {
				fprintf(stderr, "Didn't add %s to index, add manually if desired\n", full_file_name);
			}
			
			free(full_file_name);
			//TODO: check that it is a multimedia file, run tags on it and add to index
		}
		// things that are not regular files or directories are ignored
	}
	
	closedir(dir);
}

static void index_command(char *dirs[], int dircount) {
	sqlite3 *index_db = open_or_create_index_db();
	
	magic_t cookie = magic_open(MAGIC_SYMLINK | MAGIC_PRESERVE_ATIME | MAGIC_MIME_TYPE | MAGIC_ERROR | MAGIC_NO_CHECK_ASCII | MAGIC_NO_CHECK_APPTYPE | MAGIC_NO_CHECK_COMPRESS | MAGIC_NO_CHECK_ELF | MAGIC_NO_CHECK_FORTRAN | MAGIC_NO_CHECK_TAR | MAGIC_NO_CHECK_TOKENS | MAGIC_NO_CHECK_TROFF);
	
#ifdef MAGIC
	int magic_r = magic_load(cookie, MAGIC);
#else
	int magic_r = magic_load(cookie, "/usr/share/misc/magic");
#endif

	if (magic_r == -1) {
		perror("Failed to open magic file");
		exit(EXIT_FAILURE);
	}
	
	for (int i = 0; i < dircount; ++i) {
		printf("Indexing %s\n", dirs[i]);
		//TODO: check if this is a directory, if not call index_file
		index_directory(index_db, dirs[i], cookie);
	}
	
	magic_close(cookie);

	sqlite3_close(index_db);
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
// - indexing a library
// - random selection queue
// - control through stdin
// - remote control through a unix domain socket
// - remote control through dbus
// - libnotify interface
