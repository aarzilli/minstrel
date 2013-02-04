#ifndef __UTIL__
#define __UTIL__

#include <stdbool.h>
#include <sqlite3.h>
#include <libavformat/avformat.h>

void oomp(void *ptr);
bool sqlite3_has_table(sqlite3 *db, const char *name);
bool strstart(const char *haystack, const char *needle);
const char *tag_get(AVFormatContext *fmt_ctx, const char *key);
sqlite3 *open_or_create_index_db(bool truncate_tunes);
void term_init(void);
int64_t checksum(const char *a);
void putctlcod(const char *ctlcod, FILE *out);

#endif