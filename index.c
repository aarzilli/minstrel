#include "index.h"

#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libavformat/avformat.h>
#include <glib.h>

#include "util.h"

const char *KNOWN_AUDIO_EXTENSIONS[] = { "aif", "aiff", "m4a", "mid", "mp3", "mpa", "ra", "wav", "wma", "aac", "mp4", "m4p", "m4r", "3gp", "ogg", "oga", "au", "3ga", "aifc", "aifr", "alac", "caf", "caff", "opus" };

static bool should_autoindex_file(const char *full_file_name) {
	char *dot = strrchr(full_file_name, '.');
	if (dot == NULL) return false;

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

typedef struct _insert_statements {
	sqlite3_stmt *insert;
	sqlite3_stmt *rinsert;
	sqlite3_stmt *check;
} insert_statements;

static void index_file_ex(sqlite3 *index_db, insert_statements s, const char *filename,
		const char *album, const char *artist, const char *album_artist,
		const char *comment, const char *composer, const char *copyright,
		const char *date, const char *disc, const char *encoder,
		const char *genre, const char *performer, const char *publisher,
		const char *title, const char *track) {

	if (sqlite3_reset(s.insert) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_reset(s.rinsert) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_reset(s.check) != SQLITE_OK) goto index_file_ex_failure;
	
	GError *error = NULL;
	char *fileuri = g_filename_to_uri(filename, NULL, &error);
	if (fileuri == NULL) {
		fprintf(stderr, "Error converting filename [%s] into uri: %s\n", filename, error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
	}
	
	if (sqlite3_bind_text(s.check, 1, fileuri, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	
	if (sqlite3_step(s.check) != SQLITE_DONE) {
		return;
	}

	if (sqlite3_bind_text(s.insert, 1, album, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 2, artist, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 3, album_artist, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 4, comment, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 5, composer, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 6, copyright, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 7, date, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 8, disc, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 9, encoder, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 10, genre, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 11, performer, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 12, publisher, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 13, title, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.insert, 14, track, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;

	if (sqlite3_bind_text(s.insert, 15, fileuri, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;

	if (sqlite3_step(s.insert) != SQLITE_DONE) goto index_file_ex_failure;
	g_free(fileuri);

	char *text;
	asprintf(&text, "%s %s %s %s %s %s %s %s %s %s %s %s %s", album, artist, album_artist, comment, composer, copyright, date, disc, encoder, performer, publisher, title, track);
	oomp(text);

	if (sqlite3_bind_int64(s.rinsert, 1, sqlite3_last_insert_rowid(index_db)) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_bind_text(s.rinsert, 2, text, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto index_file_ex_failure;
	if (sqlite3_step(s.rinsert) != SQLITE_DONE) goto index_file_ex_failure;

	free(text);

	return;

index_file_ex_failure:

	fprintf(stderr, "Sqlite3 error in index_file_ex: %s\n", sqlite3_errmsg(index_db));
	exit(EXIT_FAILURE);
}

static void index_file(sqlite3 *index_db, insert_statements s, const char *filename) {
	AVFormatContext *fmt_ctx = NULL;

	if (avformat_open_input(&fmt_ctx, filename, NULL, NULL)) {
		fprintf(stderr, "Failed to open %s\n", filename);
		exit(EXIT_FAILURE);
	}

	//ff_metadata_conv(fmt_ctx, NULL, fmt_ctx->iformat->metadata_conv);

	const char *album = tag_get(fmt_ctx, "album");
	const char *artist = tag_get(fmt_ctx, "artist");
	const char *album_artist = tag_get(fmt_ctx, "album_artist");
	const char *comment = tag_get(fmt_ctx, "comment");
	const char *composer = tag_get(fmt_ctx, "composer");
	const char *copyright = tag_get(fmt_ctx, "copyright");
	const char *date = tag_get(fmt_ctx, "date");
	const char *disc = tag_get(fmt_ctx, "disc");
	const char *encoder = tag_get(fmt_ctx, "encoder");
	const char *genre = tag_get(fmt_ctx, "genre");
	const char *performer = tag_get(fmt_ctx, "performer");
	const char *publisher = tag_get(fmt_ctx, "publisher");
	const char *title = tag_get(fmt_ctx, "title");
	const char *track = tag_get(fmt_ctx, "track");

	if (artist == NULL) artist = album_artist;
	if (artist == NULL) artist = composer;
	if (artist == NULL) artist = copyright;
	if (artist == NULL) artist = performer;
	if (artist == NULL) artist = publisher;

	if (title == NULL) {
		char *slash = strrchr(filename, '/');
		if (slash != NULL) {
			title = slash+1;
		}
	}

#ifdef SPAM_DURING_INDEX
	printf("FILE %s:\n", filename);
	printf("   album: %s\n", album);
	printf("   artist: %s\n", artist);
	printf("   album_artist: %s\n", album_artist);
	printf("   comment: %s\n", comment);
	printf("   composer: %s\n", composer);
	printf("   copyright: %s\n", copyright);
	printf("   date: %s\n", date);
	printf("   disc: %s\n", disc);
	printf("   encoder: %s\n", encoder);
	printf("   genre: %s\n", genre);
	printf("   performer: %s\n", performer);
	printf("   publisher: %s\n", publisher);
	printf("   title: %s\n", title);
	printf("   track: %s\n", track);
#endif

	index_file_ex(index_db, s, filename,
		album, artist, album_artist,
		comment, composer, copyright,
		date, disc, encoder,
		genre, performer, publisher,
		title, track);

	avformat_close_input(&fmt_ctx);
}

static void index_directory(sqlite3 *index_db, insert_statements s, char *dir_name) {
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
			index_directory(index_db, s, new_dir_name);
			free(new_dir_name);
		} else if (curent->d_type == DT_REG) {
			char *full_file_name;

			asprintf(&full_file_name, "%s/%s", dir_name, curent->d_name);
			oomp(full_file_name);

			if (should_autoindex_file(full_file_name)) {
				index_file(index_db, s, full_file_name);
			} else {
				fprintf(stderr, "Didn't add %s to index, add manually if desired\n", full_file_name);
			}

			free(full_file_name);
		}
		// things that are not regular files or directories are ignored
	}

	closedir(dir);
}

void index_command(char *dirs[], int dircount) {
	sqlite3 *index_db = open_or_create_index_db();

	av_register_all();

	sqlite3_stmt *insert = NULL, *rinsert = NULL, *check = NULL;

	int r = sqlite3_prepare_v2(index_db, "insert into tunes(album, artist, album_artist, comment, composer, copyright, date, disc, encoder, genre, performer, publisher, title, track, filename) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);", -1, &insert, NULL);
	if (r != SQLITE_OK) {
		fprintf(stderr, "Sqlite3 error preparing insert statement: %s\n", sqlite3_errmsg(index_db));
		exit(EXIT_FAILURE);
	}

	r = sqlite3_prepare_v2(index_db, "insert into ridx(id, any) values (?, ?)", -1, &rinsert, NULL);
	if (r != SQLITE_OK) {
		fprintf(stderr, "Sqlite3 error preparing rinsert statement: %s\n", sqlite3_errmsg(index_db));
		exit(EXIT_FAILURE);
	}
	
	r = sqlite3_prepare_v2(index_db, "select * from tunes where filename = ?;", -1, &check, NULL);
	if (r != SQLITE_OK) {
		fprintf(stderr, "Sqlite3 error preparing chekc statement: %s\n", sqlite3_errmsg(index_db));
		exit(EXIT_FAILURE);
	}
	
	insert_statements ins = { insert, rinsert, check };
	
	for (int i = 0; i < dircount; ++i) {
		struct stat s;

		printf("Indexing %s\n", dirs[i]);

		if (stat(dirs[i], &s) < 0) {
			perror("Can not stat file");
			exit(EXIT_FAILURE);
		}

		if (S_ISDIR(s.st_mode)) {
			index_directory(index_db, ins, dirs[i]);
		} else {
			index_file(index_db, ins, dirs[i]);
		}
	}

	sqlite3_finalize(insert);
	sqlite3_finalize(rinsert);

	sqlite3_close(index_db);
}
