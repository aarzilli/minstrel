#ifndef __MINSTREL_STATS__
#define __MINSTREL_STATS__

#include <stdint.h>

#include <sqlite3.h>

extern sqlite3 *rating_db;

void rating_init(void);
void increment_listened(sqlite3 *index_db, sqlite3_stmt *tune_select, int64_t id);
void increment_added(sqlite3 *index_db, sqlite3_stmt *tune_select, int64_t id);

#endif
