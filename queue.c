#include "queue.h"

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

#include <termcap.h>

struct item queue[QUEUE_LENGTH];
int queue_position;
int queue_currently_playing_idx;

void queue_init(void) {
	bzero(queue, sizeof(queue));
	queue_position = 0;
	queue_currently_playing_idx = -1;
}

void queue_append(int64_t id) {
	//printf("appending to %d\n", queue_position);
	queue[queue_position].occupied = true;
	queue[queue_position].played = false;
	queue[queue_position].id = id;
	++queue_position;
}

struct item *queue_currently_playing(void) {
	return queue+queue_currently_playing_idx;
}

static int64_t random_index_item(sqlite3 *player_index_db) {
	sqlite3_stmt *random_id;

	if (sqlite3_prepare_v2(player_index_db, "select id from tunes order by random() limit 1", -1, &random_id, NULL) != SQLITE_OK) goto random_index_item_sqlite3_failure;

	if (sqlite3_step(random_id) != SQLITE_ROW) goto random_index_item_sqlite3_failure;
	int64_t id = sqlite3_column_int64(random_id, 0);
	sqlite3_finalize(random_id);

	return id;

random_index_item_sqlite3_failure:

	fprintf(stderr, "Sqlite3 error during random_index_item selection: %s\n", sqlite3_errmsg(player_index_db));
	exit(EXIT_FAILURE);
}

void advance_queue(sqlite3 *index_db) {
	if (queue_currently_playing_idx >= 0) {
		//printf("setting played to true for %d\n", queue_currently_playing_idx);
		queue[queue_currently_playing_idx].played = true;
	}

	queue_currently_playing_idx = (queue_currently_playing_idx + 1) % QUEUE_LENGTH;

	if (queue[queue_currently_playing_idx].occupied) {
		if (!queue[queue_currently_playing_idx].played) return;
	}

	// either not occupied or already played (we looped back)

	queue_append(random_index_item(index_db));
}

static void clear_screen(void) {
	fputs(tgetstr("cl", NULL), stdout);
}

void go_to_tune(sqlite3 *index_db, sqlite3_stmt *tune_select, struct item *item) {
	if (sqlite3_reset(tune_select) != SQLITE_OK) goto print_tune_sqlite_failure;
	if (sqlite3_bind_int64(tune_select, 1, item->id) != SQLITE_OK) goto print_tune_sqlite_failure;
	if (sqlite3_step(tune_select) != SQLITE_ROW) goto print_tune_sqlite_failure;

	return;

print_tune_sqlite_failure:

	fprintf(stderr, "Sqlite error while displaying queue: %s\n", sqlite3_errmsg(index_db));
	exit(EXIT_FAILURE);
}

static char *print_tune(sqlite3 *index_db, sqlite3_stmt *tune_select, struct item *item, bool current, int idx) {
	char *lyricist_link = NULL;

	go_to_tune(index_db, tune_select, item);

	if (current) {
		FILE *f = fopen("/tmp/minstrel.currently", "w");
		if (f != NULL) {
			fprintf(f, "Index: %d\n", idx);
			fprintf(f, "Title: %s\n", sqlite3_column_text(tune_select, 12));
			fprintf(f, "Author: %s\n", sqlite3_column_text(tune_select, 1));
			fprintf(f, "Album: %s\n", sqlite3_column_text(tune_select, 0));
			fprintf(f, "Track: %s\n", sqlite3_column_text(tune_select, 13));
			fclose(f);
		}

		asprintf(&lyricist_link, "Lyrics (maybe): http://lyrics.wikia.com/%s:%s", sqlite3_column_text(tune_select, 1), sqlite3_column_text(tune_select, 12));

		for (char *c = lyricist_link; *c != '\0'; ++c) {
			if (*c == ' ') *c = '_';
		}
	}

	if (idx >= 0) {
		printf(" %c %d. %s\n", current ? '>' : ' ', idx, sqlite3_column_text(tune_select, 12));
		printf(" %c\tby %s from %s [%s]\n", current ? '>' : ' ', sqlite3_column_text(tune_select, 1), sqlite3_column_text(tune_select, 0), sqlite3_column_text(tune_select, 13));
	} else {
		printf("%ld   %s\n", item->id, sqlite3_column_text(tune_select, 12));
		printf("       by %s from %s [%s]\n", sqlite3_column_text(tune_select, 1), sqlite3_column_text(tune_select, 0), sqlite3_column_text(tune_select, 13));
	}

	return lyricist_link;
}

static bool queue_could_be_prev(int idx) {
	if (!queue[idx].occupied) return false;
	if (!queue[idx].played) return false;
	return true;
}

#define DISPLAY_BEFORE_CURRENT 5
#define DISPLAY_AFTER_CURRENT 5

void display_queue(sqlite3 *index_db, sqlite3_stmt *tune_select) {
	clear_screen();

	int start_off = 1;

	while (queue_could_be_prev((queue_currently_playing_idx - start_off) % QUEUE_LENGTH)
		&& (start_off < DISPLAY_BEFORE_CURRENT))
		++start_off;

	for (int i = start_off-1; i > 0; --i) {
		int idx = (queue_currently_playing_idx - i) % QUEUE_LENGTH;
		print_tune(index_db, tune_select, queue + idx, false, idx);
	}

	char *lyricist_link = print_tune(index_db, tune_select, queue_currently_playing(), true, queue_currently_playing_idx);

	for (int i = 1; i < DISPLAY_AFTER_CURRENT; ++i) {
		int idx = (queue_currently_playing_idx + i) % QUEUE_LENGTH;
		if (!queue[idx].occupied) break;
		if (queue[idx].played) break;
		print_tune(index_db, tune_select, queue + idx, false, idx);
	}

	sqlite3_reset(tune_select);

	printf("\n");
	if (lyricist_link != NULL) {
		printf("%s\n", lyricist_link);
		free(lyricist_link);
	}
}

bool queue_to_prev(void) {
	int prev_idx = (queue_currently_playing_idx - 1) % QUEUE_LENGTH;
	if (!queue_could_be_prev(prev_idx)) return false;
	queue_currently_playing()->played = false;
	queue_currently_playing_idx = prev_idx;
	return true;
}
