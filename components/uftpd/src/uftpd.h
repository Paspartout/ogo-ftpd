#ifndef UFTPD_H

#include <netdb.h>
#include <stdbool.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

/// The class of events that the library can notify you about.
typedef enum uftpd_event {
	ServerStarted,
	ServerStopped,
	/// A client connected to the server, context contains ip as string
	ClientConnected,
	ClientDisconnected,
	Error,
} uftpd_event;

/// An event handling callback recieves the class of event and
/// optional context information.
typedef void (*uftpd_callback)(enum uftpd_event ev, const char *details);

/// Handle for every server instance.
typedef struct uftpd_ctx {
	int listen_socket;
	fd_set master;
	int fd_max;
	bool running;

	const char *start_dir;
	uftpd_callback ev_callback;
} uftpd_ctx;

/// Intitialize the given handle by setting up a socket that listents to the given addr.
int uftpd_init(uftpd_ctx *ctx, struct addrinfo *addr);

/// Use getaddrinfo and use port to intialize the server/sockets.
int uftpd_init_localhost(uftpd_ctx *ctx, const char *port);

/// Start the event loop, this functions blocks forever.
int uftpd_start(uftpd_ctx *ctx);

/// Stop the event loop.
void uftpd_stop(uftpd_ctx *ctx);

/// Set a callback function that gets called when an event happens.
void uftpd_set_ev_callback(uftpd_ctx *ctx, uftpd_callback callback);

/// Set the starting directory that is the clients first working directory.
/// NOTE: start_dir has to valid as long as the server lives.
/// No copy will be made.
void uftpd_set_start_dir(uftpd_ctx *ctx, const char *start_dir);

#define UFTPD_H
#endif
