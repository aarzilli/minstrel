#ifndef __QUEUE__
#define __QUEUE__

#include <stdint.h>
#include <stdbool.h>

#include <sqlite3.h>

struct item {
	bool occupied;
	bool played;
	int64_t id;
};

#define QUEUE_LENGTH 2048
extern struct item queue[QUEUE_LENGTH];
extern int queue_position;
extern int queue_currently_playing_idx;

void queue_init(void);
void queue_append(int64_t id);
struct item *queue_currently_playing(void);
void advance_queue(sqlite3 *index_db);

#endif