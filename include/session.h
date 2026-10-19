#ifndef SESSION_H
#define SESSION_H

/* Serve one client on this socket until it quits or disconnects. */
void handle_client(int fd);

#endif
