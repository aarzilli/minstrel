#ifndef __CONN__
#define __CONN__

#include <stdint.h>

int conn(void);
int serve(void);
void conn_and_send(int64_t cmd[2]);
void send_add(int fd, int64_t idx);

enum command_code {
	CMD_HANDSHAKE = 0,
	CMD_PLAY_PAUSE = 10,
	CMD_STOP = 11,
	CMD_NEXT = 12,
	CMD_PREV = 13,
	CMD_ADD = 20,
	CMD_REWIND = 30
};

#endif
