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
	snprintf(address->sun_path, sizeof(address->sun_path) / sizeof(char) - sizeof(char), "/tmp/minstrel.%s", getlogin());
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
	
	//TODO: handshake
	
	return fd;
}

int serve(void) {
	struct sockaddr_un address;
	setaddr(&address);
	//TODO: listen
	return -1;
}