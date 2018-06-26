#include "util.h"

#include <stdlib.h>

#include <termcap.h>

void oomp(void *ptr) {
	if (ptr == NULL) {
		perror("Out of memory");
		exit(EXIT_FAILURE);
	}
}

bool sqlite3_has_table(sqlite3 *db, const char *name) {
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

	fprintf(stderr, "Sqlite3 error on has_table: %s\n", sqlite3_errmsg(db));
	if (statement == NULL) sqlite3_finalize(statement);
	exit(EXIT_FAILURE);
}

bool strstart(const char *haystack, const char *needle) {
    return strncmp(needle, haystack, strlen(needle)) == 0;
}

const char *tag_get(AVFormatContext *fmt_ctx, const char *key) {
	AVDictionaryEntry *tag = NULL;
	tag = av_dict_get(fmt_ctx->metadata, key, NULL, AV_DICT_IGNORE_SUFFIX);
	if (tag == NULL) return NULL;
	return tag->value;
}

sqlite3 *open_or_create_db(char *name) {
	sqlite3 *db;
	int r;

	char *index_file_name;
	if (getenv("XDG_CONFIG_HOME") != NULL) {
		asprintf(&index_file_name, "%s/minstrel/%s", getenv("XDG_CONFIG_HOME"), name);
	} else {
		asprintf(&index_file_name, "%s/.config/minstrel/%s", getenv("HOME"), name);
	}
	oomp(index_file_name);

	r = sqlite3_open_v2(index_file_name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);

	oomp(db);

	if (r != SQLITE_OK) {
		fprintf(stderr, "Failed to open/create index file: %s\n", sqlite3_errmsg(db));
		exit(EXIT_FAILURE);
	}

	free(index_file_name);

	return db;
}

sqlite3 *open_or_create_index_db() {
	char *errmsg;
	sqlite3 *index_db;

	index_db =  open_or_create_db("db");

	sqlite3_exec(index_db, "pragma foreign_keys = on;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;

	sqlite3_exec(index_db, "pragma synchronous = off;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;

	sqlite3_exec(index_db, "CREATE TABLE IF NOT EXISTS config(key text, value text);", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;

	sqlite3_exec(index_db, "CREATE TABLE IF NOT EXISTS tunes(id integer primary key autoincrement, album text, artist text, album_artist text, comment text, composer text, copyright text, date text, disc text, encoder text, genre text, performer text, publisher text, title text, track text, filename text);", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;

	sqlite3_exec(index_db, "CREATE TABLE IF NOT EXISTS search_save(counter integer primary key autoincrement, id integer);", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;

	if (!sqlite3_has_table(index_db, "ridx")) {
		sqlite3_exec(index_db, "CREATE VIRTUAL TABLE ridx USING fts3(id integer, any text, foreign key (id) references tunes(id) on delete cascade deferrable initially deferred);", NULL, NULL, &errmsg);
		if (errmsg != NULL) goto open_or_create_index_db_sqlite3_failure;
	}

	return index_db;

open_or_create_index_db_sqlite3_failure:

	fprintf(stderr, "Sqlite error building index: %s\n", errmsg);
	sqlite3_free(errmsg);
	sqlite3_close(index_db);
	exit(EXIT_FAILURE);
}

void term_init(void) {
	char *termenv = getenv("TERM");

	if (termenv == NULL) return;
	if (tgetent(NULL, termenv) < 0) return;
}

int64_t checksum(const char *a) {
	int64_t r = 0;

	// TODO weak, use some crc
	for (const char *c = a; *c != '\0'; ++c) {
		r += *c;
	}

	return r;
}

void putctlcod(const char *ctlcod, FILE *out) {
	const char *x = tgetstr(ctlcod, NULL);
	if (x != NULL) fputs(x, out);
}
