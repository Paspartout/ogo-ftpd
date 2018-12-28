#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "cmds.h"
#include "queue.h"
#include "uftpd.h"

// Set PATH_MAX to 4096 for now
// Actually paths can be much longer but I don't care about that use case for
// now. (Who uses paths longer than 4096 characters anyway?)
//
// See https://eklitzke.org/path-max-is-tricky and
// https://insanecoding.blogspot.com/2007/11/pathmax-simply-isnt.html
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define USERNAME_SIZE 32
#define DATABUF_SIZE 16384

// Helper macros for less typing

#define STRLEN(s) (sizeof(s) / sizeof(s[0]))
#define UNUSED(x) (void)(x)

#ifdef DEBUG
#define dprintf(fmt, ...)                                                                          \
	do {                                                                                           \
		fprintf(stderr, fmt, __VA_ARGS__);                                                         \
	} while (0)
#else
#define dprintf(fmt, ...)
#endif

#define rreply(sock, s)                                                                            \
	if (send(sock, s, STRLEN(s), 0) == -1)                                                         \
		return -1;

#define rreply_client(s) rreply(client->socket, s)

#define rreplyf(s, fmt, ...)                                                                       \
	if (replyf(s, fmt, __VA_ARGS__) == -1)                                                         \
		return -1;

#define notify_user(ev, data)                                                                      \
	do {                                                                                           \
		if (ev_callback != NULL)                                                                   \
			ev_callback(ev, data);                                                                 \
	} while (0)

#define notify_user_ctx(ev, data)                                                                  \
	do {                                                                                           \
		if (ctx->ev_callback != NULL)                                                              \
			ctx->ev_callback(ev, data);                                                            \
	} while (0)

#define rpath_extend(dest, n, base, child)                                                         \
	if (path_extend(dest, n, base, child) < 0) {                                                   \
		replyf(client->socket, "500 Internal error: Path is too long!");                           \
		return -1;                                                                                 \
	}

static int replyf(int sock, const char *format, ...) {
#define REPLYBUFLEN 255
	static char reply_buf[REPLYBUFLEN];

	va_list args;
	va_start(args, format);
	int len = vsnprintf(reply_buf, REPLYBUFLEN, format, args);
	va_end(args);

	if (send(sock, reply_buf, len, 0) == -1)
		return -1;

	return 0;
}

// Appends the path child to base and writes the result to dest.
static int path_extend(char *dest, size_t n, const char *base, const char *child) {
	int len = snprintf(dest, n, "%s/%s", base, child);
	if (len > (int)n) {
		fprintf(stderr, "the new path is too long!\n");
		return -1;
	} else if (len < 0) {
		perror("snprintf");
		return -2;
	}
	return 0;
}

// TODO: Consider implementing proper authentication or anonymous login
static bool check_login(const char *username, const char *password) {
	UNUSED(username);
	UNUSED(password);
	dprintf("USER \"%s\" logged in with PASS \"%s\"\n", username, password);
	return true;
}

enum ClientState {
	Disconnected = 0,
	Identifying,    // Before submitting username
	Authenticating, // Before authenticated using username and password
	LoggedIn,
};

// The data representation type used for data transfer and storage.
enum TranfserType {
	Image,
	Ascii,
};

// The data structure type used for data transfer and storage.
enum StructureType {
	File,
	Record,
	Page,
};

typedef struct Client {
	enum ClientState state;
	int socket;
	int data_socket;

	char username[USERNAME_SIZE];
	char cwd[PATH_MAX];
	enum TranfserType ttype;
	enum StructureType stype;

	// Adress and port used for active or passive ftp?
	bool passive_mode;
	struct sockaddr_in addr;

	// Used for moving/renaming files
	char from_path[PATH_MAX];

	// For linked list
	SLIST_ENTRY(Client) entries;
} Client;

// Creates the list head struct
SLIST_HEAD(ClientList, Client)
client_list = SLIST_HEAD_INITIALIZER(client_list);
static bool list_initialized = false;

// Retrieve client struct by its socket
static Client *get_client(int socket) {
	Client *c;
	SLIST_FOREACH(c, &client_list, entries) {
		if (c->socket == socket || c->data_socket == socket) {
			return c;
		}
	}

	return NULL;
}

// Disconnects all clients and frees their memory
static int disconnect_all_clients() {
	while (!SLIST_EMPTY(&client_list)) {
		Client *c = SLIST_FIRST(&client_list);
		SLIST_REMOVE_HEAD(&client_list, entries);
		if (close(c->socket) == -1) {
			perror("close");
		}
		free(c);
	}
	return 0;
}

// Create a new client and initalize it
static Client *client_new(int socket, struct sockaddr_storage *client_addr, const char *start_dir) {
	Client *new_client = malloc(sizeof(Client));
	if (new_client == NULL) {
		fprintf(stderr, "error mallocing client!\n");
		return NULL;
	}

	new_client->socket = socket;
	new_client->state = Identifying;
	new_client->data_socket = -1;
	new_client->ttype = Image;
	new_client->passive_mode = false;
	new_client->from_path[0] = 0;
	strncpy(new_client->cwd, start_dir, PATH_MAX);

	// Use client address and default port 20 for active mode
	new_client->addr.sin_port = htons(20);
	memcpy(&(new_client->addr), client_addr, sizeof(new_client->addr));

	return new_client;
}

// Handle a new incomming connection on listen_sock by
// creating a new client and welcome it to the server.
static int handle_connect(int listen_sock, uftpd_ctx *ctx) {
	struct sockaddr_storage client_addr;
	socklen_t addrlen = sizeof(client_addr);

	int newfd = accept(listen_sock, (struct sockaddr *)&client_addr, &addrlen);
	if (newfd == -1) {
		perror("accept");
		return newfd;
	}

	if (!list_initialized) {
		SLIST_INIT(&client_list);
		list_initialized = true;
	}

	// Insert client into list of connected clients
	Client *client = client_new(newfd, &client_addr, ctx->start_dir);
	if (client == NULL) {
		return -1;
	}
	SLIST_INSERT_HEAD(&client_list, client, entries);

	rreply_client("220 uftpd server\r\n");

	char client_ipstr[INET6_ADDRSTRLEN];
	inet_ntop(client_addr.ss_family, &((struct sockaddr_in *)&client_addr)->sin_addr, client_ipstr,
	          sizeof(client_ipstr));
	notify_user_ctx(ClientConnected, client_ipstr);

	return newfd;
}

// Handle a disconnect by removing the client from
// the connected client list and freeing its memory.
static void handle_disconnect(int client_sock, uftpd_callback ev_callback) {
	UNUSED(ev_callback);
	Client *client = get_client(client_sock);
	assert(client != NULL);

	char client_ipstr[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET, &((struct sockaddr_in *)&client->addr)->sin_addr, client_ipstr,
	          sizeof(client_ipstr));
	notify_user(ClientDisconnected, client_ipstr);

	SLIST_REMOVE(&client_list, client, Client, entries);
	free(client);
}

// Process data from data connection
static int handle_data(char *buf, Client *client) {
	UNUSED(buf);
	UNUSED(client);
	// TODO: PASSV: Implement
	return 0;
}

static int cwd(Client *client, const char *path) {
	static char pathbuf[8192];
	const char *newpath;
	const char *pwd = client->cwd;

	// Handle .. and .
	if (path[0] == '.' && path[1] == '.') {
		// Go up
		if (strlen(pwd) <= 1 && pwd[0] == '/') {
			// Cant go up anymore
			rreply_client("431 Error changing directory: Already at topmost directory\n");
			return -1;
		}

		// Remove upmost directory
		bool copy = false;
		int len = 0;
		for (ssize_t i = strlen(pwd); i >= 0; i--) {
			if (pwd[i] == '/')
				copy = true;
			if (copy) {
				pathbuf[i] = pwd[i];
				len++;
			}
		}
		assert(copy);
		// remove trailing slash
		if (len > 1 && pathbuf[len - 1] == '/')
			len--;
		pathbuf[len] = '\0';
		newpath = pathbuf;
	} else if (path[0] == '/') {
		// Go to specified absoule path
		newpath = path;
	} else {
		// Go to specified relative path
		rpath_extend(pathbuf, sizeof(pathbuf), client->cwd, path);
		newpath = pathbuf;
	}

	// Make sure the folder exists
	struct stat dest_stat;
	if (stat(newpath, &dest_stat) == -1) {
		replyf(client->socket, "431 Error changing directory: %s\n", strerror(errno));
		return -1;
	}
	if (!S_ISDIR(dest_stat.st_mode)) {
		replyf(client->socket, "431 %s is not a directory!\n", path);
		return -1;
	}

	strncpy(client->cwd, newpath, PATH_MAX);
	rreply_client("200 Working directory changed.\n");
	return 0;
}

// Open a active ftp connection by connecting to the clients address
static int open_active(Client *client) {
	int data_socket = socket(AF_INET, SOCK_STREAM, 0);
	// TODO: Consider using getaddrinfo
	if (data_socket == -1) {
		replyf(client->socket, "500 Connection error: %s\n", strerror(errno));
		perror("socket");
		return -1;
	}

	rreply_client("150 File status okay; about to open data connection.\n");
	int res = connect(data_socket, (struct sockaddr *)&(client->addr), sizeof(client->addr));
	if (res == -1) {
		replyf(client->socket, "500 Connection error: %s\n", strerror(errno));
		perror("connect");
		close(data_socket);
		return -1;
	}

	return data_socket;
}

// Open active or passive connection depending on setting
static int open_data(Client *client) {
	int data_socket;
	if (client->passive_mode) {
		// TODO: PASSV: Use listen socket? Enque listen command?
	} else {
		if ((data_socket = open_active(client)) == -1) {
			return -1;
		}
		return data_socket;
	}
	return -1;
}

// Handle commands from user once logged in.
// Return -2 on usage error.
// Return -1 on network/critical error.
static int handle_ftpcmd_logged_in(const FtpCmd *cmd, Client *client) {
	static char fullpath[PATH_MAX];
	const int client_sock = client->socket;
	int data_socket;
	char type;
	switch (cmd->keyword) {
	case PWD: // Print working directory
		rreplyf(client_sock, "257 \"%s\"\n", client->cwd);
		break;
	case CWD: // Change working directory
		if (cwd(client, cmd->parameter.string) == -1) {
			return -2;
		}
		break;
	case CDUP:
		if (cwd(client, "..") == -1) {
			return -2;
		}
		break;
	case PORT: {
		const uint8_t ip0 = cmd->parameter.numbers[0];
		const uint8_t ip1 = cmd->parameter.numbers[1];
		const uint8_t ip2 = cmd->parameter.numbers[2];
		const uint8_t ip3 = cmd->parameter.numbers[3];

		const uint8_t port0 = cmd->parameter.numbers[4]; // high byte
		const uint8_t port1 = cmd->parameter.numbers[5]; // low byte
		client->addr.sin_family = AF_INET;
		// TODO: Probably don't do this and use inet_pton
		client->addr.sin_addr.s_addr = ip3 << 24 | ip2 << 16 | ip1 << 8 | ip0;
		client->addr.sin_port = port1 << 8 | port0;
		rreply_client("200 PORT was set.\n");
	} break;
	// case PASV:
	// TODO: PASSV: Listen on new dataport and reply with addr of it
	// break;
	case RETR: {
		// Do it
		rpath_extend(fullpath, PATH_MAX, client->cwd, cmd->parameter.string);
		dprintf("opening file %s\n", fullpath);

		FILE *f = fopen(fullpath, "r");
		if (f == NULL) {
			replyf(client->socket, "550 Filesystem error: %s\n", strerror(errno));
			perror("fopen");
			return -2;
		}

		if ((data_socket = open_data(client)) == -1) {
			return -1;
		}

		char data_buf[DATABUF_SIZE];
		size_t read_bytes = 0;
		ssize_t sent_bytes = 0;
		size_t reamining_bytes = 0;
		do {
			read_bytes = fread(data_buf, 1, 2048, f);
			dprintf("read %ld bytes\n", read_bytes);
			if (ferror(f)) {
				perror("fread");
			}
			reamining_bytes = read_bytes;
			dprintf("remaining %ld bytes\n", reamining_bytes);
			while (reamining_bytes > 0) {
				sent_bytes = send(data_socket, data_buf, read_bytes, 0);
				dprintf("sent %ld bytes\n", sent_bytes);
				if (sent_bytes == -1) {
					perror("send");
					close(data_socket);
					return -2;
				}
				reamining_bytes -= sent_bytes;
				dprintf("remaining %ld bytes\n", reamining_bytes);
			}
		} while (read_bytes > 0);

		fclose(f);

		rreply_client("226 Closing data connection.\n");
		close(data_socket);
		rreply_client("250 Requested file action okay, completed.\n");
	} break;
	case STOR: {
		rpath_extend(fullpath, PATH_MAX, client->cwd, cmd->parameter.string);

		// Try to create file by opening it for writing
		FILE *f = fopen(fullpath, "w");
		if (f == NULL) {
			replyf(client->socket, "550 Filesystem error: %s\n", strerror(errno));
			perror("fopen");
			return -2;
		}

		if ((data_socket = open_data(client)) == -1) {
			return -1;
		}

		// Read data from data_socket and write it to created file
		char data_buf[DATABUF_SIZE];
		size_t written_bytes = 0;
		ssize_t received_bytes = 0;
		ssize_t reamining_bytes = 0;
		do {
			received_bytes = recv(data_socket, data_buf, sizeof(data_buf), 0);
			if (received_bytes == -1) {
				perror("recv");
				return -1;
			}
			dprintf("received %ld bytes\n", received_bytes);
			reamining_bytes = received_bytes;
			dprintf("remaining %ld bytes\n", reamining_bytes);
			// Write the received bytes down
			while (reamining_bytes > 0) {
				written_bytes = fwrite(data_buf, 1, received_bytes, f);
				dprintf("written %ld bytes\n", written_bytes);
				if (ferror(f)) {
					replyf(client->socket, "550 Filesystem error: %s\n", strerror(errno));
					perror("fwrite");
					fclose(f);
					return -1;
				}
				reamining_bytes -= written_bytes;
				dprintf("remainingloop %ld bytes\n", reamining_bytes);
			}
		} while (received_bytes > 0);

		rreply_client("226 Closing data connection.\n");
		close(data_socket);
		rreply_client("250 Requested file action okay, completed.\n");

		fclose(f);
	} break;
	case DELE: {
		rpath_extend(fullpath, PATH_MAX, client->cwd, cmd->parameter.string);
		if (unlink(fullpath) == -1) {
			perror("unlink");
			rreplyf(client->socket, "550 Filesystem error: %s\n", strerror(errno));
			return -2;
		}
		rreply_client("250 Requested file action okay, completed.\n");
	} break;
	case RMD: {
		rpath_extend(fullpath, PATH_MAX, client->cwd, cmd->parameter.string);
		if (rmdir(fullpath) == -1) {
			perror("rmdir");
			rreplyf(client->socket, "550 Filesystem error: %s\n", strerror(errno));
			return -2;
		}
		rreply_client("250 Requested file action okay, completed.\n");
	} break;
	case MKD: {
		rpath_extend(fullpath, PATH_MAX, client->cwd, cmd->parameter.string);
		if (mkdir(fullpath, 0755) == -1) {
			perror("mkdir");
			rreplyf(client->socket, "550 Filesystem error: %s\n", strerror(errno));
			return -2;
		}
		rreply_client("250 Requested file action okay, completed.\n");
	} break;
	case RNFR: {
		rpath_extend(client->from_path, PATH_MAX, client->cwd, cmd->parameter.string);
		rreply_client("350 Please specify destination using RNTO now.\n");
	} break;
	case RNTO: {
		if (client->from_path[0] == 0) {
			rreply_client("503 Bad sequence of commands. Use RNFR first.\n");
			return -2;
		}
		if (rename(client->from_path, cmd->parameter.string) == -1) {
			perror("rename");
			rreplyf(client->socket, "550 Filesystem error: %s\n", strerror(errno));
			client->from_path[0] = 0;
			return -2;
		}
		client->from_path[0] = 0;
		rreply_client("250 Requested file action okay, completed.\n");
	} break;
	case LIST: {
		const char *pathname = client->cwd;
		if (cmd->parameter.string[0] != 0) {
			pathname = cmd->parameter.string;
		}

		// List files
		// TODO: Buffer first and the send once we know we got no error
		DIR *dir = opendir(pathname);
		if (dir == NULL) {
			rreplyf(client->socket, "450 Filesystem error: %s\n", strerror(errno));
			perror("opendir");
			return -2;
		}

		if ((data_socket = open_data(client)) == -1) {
			return -1;
		}

		struct dirent *entry;
		while ((entry = readdir(dir)) != NULL) {
			if (entry->d_name[0] == '.') { // skip . file for now
				continue;
			}
			struct stat entry_stat;
			rpath_extend(fullpath, sizeof(fullpath), client->cwd, entry->d_name);
			if (stat(fullpath, &entry_stat) == -1) {
				perror("stat");
				continue;
			}

			// filetype: no link support(yet?)
			char filetype;
			if (entry->d_type == DT_DIR)
				filetype = 'd';
			else
				filetype = '-';

			// Date format conforming to POSIX ls:
			// https://pubs.opengroup.org/onlinepubs/9699919799/utilities/ls.html
			char date_str[24];
			const char *date_fmt = "%b %d %H:%M";
			time_t mtime = entry_stat.st_mtime;
			// Display year if file is older than 6 months
			if (time(NULL) > entry_stat.st_mtime + 6 * 30 * 24 * 60 * 60)
				date_fmt = "%b %d  %Y";
			strftime(date_str, 24, date_fmt, localtime(&mtime));

			const char *fmtstring = "%crw-rw-rw- 1 user group %lu %s %s\r\n";
			dprintf(fmtstring, filetype, entry_stat.st_size, date_str, entry->d_name);
			rreplyf(data_socket, fmtstring, filetype, entry_stat.st_size, date_str, entry->d_name);
		}
		closedir(dir);

		rreply_client("226 Closing data connection.\n");
		close(data_socket);
		rreply_client("250 Requested file action okay, completed.\n");
	} break;
	case TYPE: // Set the data representation type
		type = cmd->parameter.code;
		if (type == 'I') {
			client->ttype = Image;
			rreplyf(client_sock, "200 Type set to %c.\n", type);
		} else {
			rreplyf(client_sock, "500 Type %c not supported.\n", type);
		}
		break;
	case STRU: // Set the data structure
		type = cmd->parameter.code;
		if (type == 'F') {
			client->stype = File;
			rreplyf(client_sock, "200 Structure set to %c.\n", type);
		} else {
			rreplyf(client_sock, "500 Type %c not supported.\n", type);
		}
		break;
	case NOOP:
		rreply(client_sock, "200 Successfully did nothing.\n");
		break;
	case INVALID:
		rreply(client_sock, "500 Invalid command.\n");
		break;
	default:
		rreply(client_sock, "502 Command parsed but not implemented yet.\n");
		return -1;
		break;
	}
	return 0;
}

// Hande ftp command depending on the clients state.
static int handle_ftpcmd(FtpCmd *cmd, Client *client, uftpd_callback ev_callback) {
	switch (client->state) {
	case Identifying:
		// Only allow USER command for identification
		if (cmd->keyword == USER) {
			rreply_client("331 Please authenticate using PASS.\n");
			client->state = Authenticating;
			strncpy(client->username, cmd->parameter.string, USERNAME_SIZE);
		} else {
			rreply_client("530 Please login using USER and PASS command.\n");
		}
		break;
	case Authenticating:
		// Only allow PASS command for authentification
		if (cmd->keyword == PASS) {
			// check username and password
			const char *password = cmd->parameter.string;
			bool logged_in = check_login(client->username, password);
			if (logged_in) {
				rreply_client("230 Login successful.\n");
				client->state = LoggedIn;
			} else {
				rreply_client("530 Wrong password.\n");
				client->state = Identifying;
			}
		} else {
			rreply_client("530 Please use PASS to authenticate.\n");
		}
		break;
	case LoggedIn:
		// Allow every command
		if (handle_ftpcmd_logged_in(cmd, client) == -1) {
			notify_user(Error, "FTP Network error");
		}
		break;
	default:
		fprintf(stderr, "invalid client state: %d\n", client->state);
		break;
	}
	return 0;
}

// Handle authentication and socket re
static int handle_recv(int client_sock, char *buf, uftpd_callback ev_callback) {
	// Retreive user socket
	Client *client = get_client(client_sock);
	assert(client != NULL);
	const bool is_data_socket = client_sock == client->data_socket ? true : false;

	// TODO: PASSV: Handle DTP socket?
	if (is_data_socket) {
		if (handle_data(buf, client) != 0) {
			fprintf(stderr, "error handling ftp data socket");
			return -1;
		}
		return 0;
	}

	// Parse and execute ftp command
	FtpCmd cmd = parse_ftpcmd(buf);
	dprintf("command buffer: \"%s\"", buf);
	dprintf("parsed command: %s\n", keyword_names[cmd.keyword]);
	return handle_ftpcmd(&cmd, client, ev_callback);
}

// ============================================================================
// Public API
// ============================================================================
int uftpd_init_localhost(uftpd_ctx *ctx, const char *port) {
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(NULL, port, &hints, &res) == -1) {
		perror("getaddrinfo");
		return -1;
	}

	if (uftpd_init(ctx, res) == -1)
		return -1;

	freeaddrinfo(res);

	return 0;
}

// Prepare to go into listen/event loop
int uftpd_init(uftpd_ctx *ctx, struct addrinfo *addr) {
	int listen_socket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	if (listen_socket == -1) {
		perror("socket");
		return -1;
	}

	if (bind(listen_socket, addr->ai_addr, sizeof(struct sockaddr)) == -1) {
		perror("bind");
		return -1;
	}

	if (listen(listen_socket, 10) == -1) {
		perror("listen");
		return -1;
	}

	FD_ZERO(&ctx->master);

	FD_SET(listen_socket, &ctx->master);
	ctx->fd_max = listen_socket;
	ctx->listen_socket = listen_socket;
	ctx->running = true;
	ctx->ev_callback = NULL;
	ctx->start_dir = "/";

	return 0;
}

// Event loop of server
int uftpd_start(uftpd_ctx *ctx) {
	int nbytes;
	fd_set ready;
	char buf[128];

	FD_ZERO(&ready);

	notify_user_ctx(ServerStarted, NULL);
	while (ctx->running) {
		ready = ctx->master;
		if (select(ctx->fd_max + 1, &ready, NULL, NULL, NULL) == -1) {
			perror("select");
			break;
		}
		int fdmax = ctx->fd_max;

		for (int sock = 0; sock <= fdmax; sock++) {
			if (!FD_ISSET(sock, &ready)) {
				continue;
			}

			if (sock == ctx->listen_socket) {
				int csock;
				if ((csock = handle_connect(ctx->listen_socket, ctx)) == -1) {
					fprintf(stderr, "error handling incomming connection");
					ctx->running = false;
				}

				// Add new socket to master list
				FD_SET(csock, &ctx->master);
				if (csock > fdmax) {
					fdmax = csock;
				}
			} else { // data socket
				if ((nbytes = recv(sock, buf, sizeof(buf), 0)) <= 0) {
					// ctx error or disconnect
					if (nbytes == 0) {
						handle_disconnect(sock, ctx->ev_callback);
					} else {
						perror("recv");
					}
					close(sock);
					FD_CLR(sock, &ctx->master);
					continue; // skip to next socket
				}

				// nbytes > 0
				assert(nbytes > 0);
				buf[nbytes] = '\0';
				if (handle_recv(sock, buf, ctx->ev_callback) == -1) {
					fprintf(stderr, "error handling package\n");
					ctx->running = false;
				}
			} // sock == listen_sock
			ctx->fd_max = fdmax;
		} // for all sockets
	}     // while(running)

	// close remaining connections
	disconnect_all_clients();
	close(ctx->listen_socket);
	notify_user_ctx(ServerStopped, NULL);
	return 0;
}

void uftpd_stop(uftpd_ctx *ctx) { ctx->running = false; }
void uftpd_set_ev_callback(uftpd_ctx *ctx, uftpd_callback callback) { ctx->ev_callback = callback; }
void uftpd_set_start_dir(uftpd_ctx *ctx, const char *start_dir) { ctx->start_dir = start_dir; }
