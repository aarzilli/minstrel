#include "conn.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>

static void setaddr(struct sockaddr_un *address) {
	bzero(address, sizeof(*address));

	//printf("Size of path: %zd\n", sizeof(address.sun_path) / sizeof(char) - sizeof(char));
	address->sun_family = AF_UNIX;
	snprintf(address->sun_path, sizeof(address->sun_path) / sizeof(char) - sizeof(char), "/tmp/minstrel.%d", getuid());
}

int conn(void) {
	struct sockaddr_un address;
	setaddr(&address);
	
	int fd = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("Couldn't create a unix domain socket\n");
		exit(EXIT_FAILURE);
	}

	if (connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)) != 0) {
		close(fd);
		return -1;
	}
	
	// Handshake
	
	int64_t hs[2] = { CMD_HANDSHAKE, 0 };
	int r = send(fd, (void *)hs, sizeof(hs), 0);
	if (r != sizeof(hs)) {
		fprintf(stderr, "Couldn't send handshake %d\n", r);
		close(fd);
		return -1;
	}
	
	return fd;
}

int serve(void) {
	struct sockaddr_un address;
	setaddr(&address);
	
	unlink(address.sun_path);
	
	int fd = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("Couldn't create a unix domain socket\n");
		exit(EXIT_FAILURE);
	}
	
	if (bind(fd, (struct sockaddr *) &address, sizeof(address)) != 0) {
		perror("Couldn't open control socket");
		exit(EXIT_FAILURE);
	}
	
	return fd;
}

void conn_and_send(int64_t cmd[2]) {
	int fd = conn();
	if (fd == -1) return;
	send(fd, (void *)cmd, sizeof(int64_t)*2, 0);
	close(fd);
}

void send_add(int fd, int64_t idx) {
	int64_t cmd[] = { CMD_ADD, idx };
	send(fd, (void *)cmd, sizeof(cmd), 0);
	return;
}
