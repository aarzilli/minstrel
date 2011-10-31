#include "queue.h"

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

struct item queue[QUEUE_LENGTH];
int queue_position;
int queue_currently_playing_idx;

void queue_init(void) {
	bzero(queue, sizeof(queue));
	queue_position = 0;
	queue_currently_playing_idx = -1;
}

void queue_append(int64_t id) {
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
	if (queue_currently_playing_idx >= 0)
		queue[queue_currently_playing_idx].played = true;
		
	queue_currently_playing_idx = (queue_currently_playing_idx + 1) % QUEUE_LENGTH;
	
	if (queue[queue_currently_playing_idx].occupied) {
		if (!queue[queue_currently_playing_idx].played) return;
	}
	
	// either not occupied or already played (we looped back)
	
	queue_append(random_index_item(index_db));
}

void display_queue(void) {
	//TODO:
	// - clear screen
	// - display last 5 played items (when existing)
	// - display currently playing item in bold with a prepended '>'
	// - display up to 5 upcoming items (they must have occupied == true and played == false)
}