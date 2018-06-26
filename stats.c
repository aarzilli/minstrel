#include "stats.h"

#include <stdlib.h>

#include "queue.h"
#include "util.h"

sqlite3 *rating_db = NULL;

void rating_init(void) {
	char *errmsg;

	rating_db = open_or_create_db("rating");

	sqlite3_exec(rating_db, "pragma foreign_keys = on;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto rating_init_failure;

	sqlite3_exec(rating_db, "pragma synchronous = off;", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto rating_init_failure;

	sqlite3_exec(rating_db, "CREATE TABLE IF NOT EXISTS rating(filename text primary key, listened integer default 0, added integer default 0);", NULL, NULL, &errmsg);
	if (errmsg != NULL) goto rating_init_failure;

	return;

rating_init_failure:

	fprintf(stderr, "Sqlite error building rating db: %s\n", errmsg);
	sqlite3_free(errmsg);
	sqlite3_close(rating_db);
	exit(EXIT_FAILURE);
}

void increment_listened(sqlite3 *index_db, sqlite3_stmt *tune_select, int64_t id) {
	const unsigned char *path;
	sqlite3_stmt *update_stmt;

	go_to_tune(index_db, tune_select, id);
	path = sqlite3_column_text(tune_select, 14);

	if (sqlite3_prepare_v2(rating_db, "insert or ignore into rating(filename) values (?)", -1, &update_stmt, NULL) != SQLITE_OK) goto increment_listened_failure;

	if (sqlite3_bind_text(update_stmt, 1, (const char *)path, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto increment_listened_failure;

	if (sqlite3_step(update_stmt) != SQLITE_DONE) goto increment_listened_failure;

	sqlite3_finalize(update_stmt);

	if (sqlite3_prepare_v2(rating_db, "update rating set listened = listened + 1 where filename = ?", -1, &update_stmt, NULL) != SQLITE_OK) goto increment_listened_failure;

	if (sqlite3_bind_text(update_stmt, 1, (const char *)path, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto increment_listened_failure;

	if (sqlite3_step(update_stmt) != SQLITE_DONE) goto increment_listened_failure;

	sqlite3_finalize(update_stmt);

	return;

increment_listened_failure:

	fprintf(stderr, "could not increment listened count\n");
}

void increment_added(sqlite3 *index_db, sqlite3_stmt *tune_select, int64_t id) {
	const unsigned char *path;
	sqlite3_stmt *update_stmt;

	go_to_tune(index_db, tune_select, id);
	path = sqlite3_column_text(tune_select, 14);

	if (sqlite3_prepare_v2(rating_db, "insert or ignore into rating(filename) values (?)", -1, &update_stmt, NULL) != SQLITE_OK) goto increment_added_failure;

	if (sqlite3_bind_text(update_stmt, 1, (const char *)path, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto increment_added_failure;

	if (sqlite3_step(update_stmt) != SQLITE_DONE) goto increment_added_failure;

	sqlite3_finalize(update_stmt);

	if (sqlite3_prepare_v2(rating_db, "update rating set added = added + 1 where filename = ?", -1, &update_stmt, NULL) != SQLITE_OK) goto increment_added_failure;

	if (sqlite3_bind_text(update_stmt, 1, (const char *)path, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto increment_added_failure;

	if (sqlite3_step(update_stmt) != SQLITE_DONE) goto increment_added_failure;

	sqlite3_finalize(update_stmt);

	return;

increment_added_failure:

	fprintf(stderr, "could not increment added count\n");
}

