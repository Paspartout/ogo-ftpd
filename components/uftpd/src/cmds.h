#ifndef CMD_H
#define CMD_H
#include <stdint.h>

// FtpKeyword lists all ftp commands
// see https://tools.ietf.org/html/rfc959
enum FtpKeyword {
	INVALID,
	USER,            // <SP> <username> <CRLF>
	PASS,            // <SP> <password> <CRLF>
	ACCT,            // <SP> <account-information> <CRLF>
	CWD,             // <SP> <pathname> <CRLF>
	CDUP,            // <CRLF>
	SMNT,            // <SP> <pathname> <CRLF>
	QUIT,            // <CRLF>
	REIN,            // <CRLF>
	PORT,            // <SP> <host-port> <CRLF>
	PASV,            // <CRLF>
	TYPE,            // <SP> <type-code> <CRLF>
	STRU,            // <SP> <structure-code> <CRLF>
	MODE,            // <SP> <mode-code> <CRLF>
	RETR,            // <SP> <pathname> <CRLF>
	STOR,            // <SP> <pathname> <CRLF>
	STOU,            // <CRLF>
	APPE,            // <SP> <pathname> <CRLF>
	ALLO,            // <SP> <decimal-integer>
	                 // [<SP> R <SP> <decimal-integer>] <CRLF>
	REST,            // <SP> <marker> <CRLF>
	RNFR,            // <SP> <pathname> <CRLF>
	RNTO,            // <SP> <pathname> <CRLF>
	ABOR,            // <CRLF>
	DELE,            // <SP> <pathname> <CRLF>
	RMD,             // <SP> <pathname> <CRLF>
	MKD,             // <SP> <pathname> <CRLF>
	PWD,             // <CRLF>
	LIST,            // [<SP> <pathname>] <CRLF>
	NLST,            // [<SP> <pathname>] <CRLF>
	SITE,            // <SP> <string> <CRLF>
	SYST,            // <CRLF>
	STAT,            // [<SP> <pathname>] <CRLF>
	HELP,            // [<SP> <string>] <CRLF>
	NOOP,            // <CRLF>
	NUM_FTPKEYWORDS, // is set to number of commands
};

extern const char *keyword_names[];

#define MAX_STRSIZE 512

typedef struct FtpCmd {
	enum FtpKeyword keyword;

	// Parameters
	union {
		char string[MAX_STRSIZE]; // for pathname, username, ...
		uint8_t numbers[6];       // host-number followed by port-number
		char code;                // type/mode/structure code
	} parameter;
} FtpCmd;

// Parse a zero terimated string into a ftp command
FtpCmd parse_ftpcmd(const char *YYCURSOR);

#endif /* end of include guard */
