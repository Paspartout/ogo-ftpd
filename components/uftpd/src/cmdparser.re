#include "cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// TODO: Probably move somewhere else?
const char *keyword_names[] = {
    "INVALID",
    "USER",           // <SP> <username> <CRLF>
    "PASS",           // <SP> <password> <CRLF>
    "ACCT",           // <SP> <account-information> <CRLF>
    "CWD",            // <SP> <pathname> <CRLF>
    "CDUP",           // <CRLF>
    "SMNT",           // <SP> <pathname> <CRLF>
    "QUIT",           // <CRLF>
    "REIN",           // <CRLF>
    "PORT",           // <SP> <host-port> <CRLF>
    "PASV",           // <CRLF>
    "TYPE",           // <SP> <type-code> <CRLF>
    "STRU",           // <SP> <structure-code> <CRLF>
    "MODE",           // <SP> <mode-code> <CRLF>
    "RETR",           // <SP> <pathname> <CRLF>
    "STOR",           // <SP> <pathname> <CRLF>
    "STOU",           // <CRLF>
    "APPE",           // <SP> <pathname> <CRLF>
    "ALLO",           // <SP> <decimal-integer>
                      // [<SP> R <SP> <decimal-integer>] <CRLF>
    "REST",           // <SP> <marker> <CRLF>
    "RNFR",           // <SP> <pathname> <CRLF>
    "RNTO",           // <SP> <pathname> <CRLF>
    "ABOR",           // <CRLF>
    "DELE",           // <SP> <pathname> <CRLF>
    "RMD",            // <SP> <pathname> <CRLF>
    "MKD",            // <SP> <pathname> <CRLF>
    "PWD",            // <CRLF>
    "LIST",           // [<SP> <pathname>] <CRLF>
    "NLST",           // [<SP> <pathname>] <CRLF>
    "SITE",           // <SP> <string> <CRLF>
    "SYST",           // <CRLF>
    "STAT",           // [<SP> <pathname>] <CRLF>
    "HELP",           // [<SP> <string>] <CRLF>
    "NOOP",           // <CRLF>
    "NUM_FTPKEYWORDS" // is set to number of commands
};

#define CMD_NOPARAM(KEYWORD)                                                                       \
	cmd.keyword = KEYWORD;                                                                         \
	goto done;

#define CMD_STRING(KEYWORD)                                                                        \
	cmd.keyword = KEYWORD;                                                                         \
	size_t len = p2 - p1 > MAX_STRSIZE ? MAX_STRSIZE : p2 - p1;                                    \
	memcpy(&cmd.parameter.string, p1, len);                                                        \
	cmd.parameter.string[len] = '\0';                                                              \
	goto done;

#define CMD_OPT_STRING(KEYWORD)                                                                    \
	cmd.keyword = KEYWORD;                                                                         \
	size_t len = p2 - p1 > MAX_STRSIZE ? MAX_STRSIZE : p2 - p1;                                    \
	if (len > 0) {                                                                                 \
		memcpy(&cmd.parameter.string, p1, len);                                                    \
		cmd.parameter.string[len] = '\0';                                                          \
	}                                                                                              \
	goto done;

FtpCmd parse_ftpcmd(const char *YYCURSOR) {
	FtpCmd cmd;
	cmd.parameter.string[0] = 0; // 0 marks no string option given
	cmd.keyword = INVALID;

	const char *YYMARKER;
	const char *p1, *p2, *p3, *p4, *p5, *p6;
	/*!stags:re2c format = 'const char *@@;'; */
	/*!re2c
	    re2c:define:YYCTYPE = char;
	    re2c:yyfill:enable = 0;

	    end = "\x00" | "\n" | "\r\n";
	    char = [^ \t\r\n\x00];
	    char_and_ws = [^\r\n\x00];
	    string = char char_and_ws*;
	    digit = [0-9];
	    number
	        = digit
	        | [1-9] digit
	        | "1" digit{2}
	        | "2" [0-4] digit
	        | "25" [0-5];
	    sp = [ \t]+;
	    typecode = "A" | "E" | "I" | "L"; // No proper support for L
	    structurecode = "F" | "R" | "P";
	    modecode = "S" | "B" | "C";

	    hostnumber = @p1 number "," @p2 number "," @p3 number "," @p4 number;
	    portnumber = @p5 number "," @p6 number;
	    hostport = hostnumber "," portnumber;

	    * { goto done; }
	    "USER" sp @p1 string @p2 end { CMD_STRING(USER) }
	    "PASS" sp @p1 string @p2 end { CMD_STRING(PASS) }
	    "ACCT" sp @p1 string @p2 end { CMD_STRING(ACCT) }
	    "CWD"  sp @p1 string @p2 end { CMD_STRING(CWD)  }
	    "CDUP" end { CMD_NOPARAM(CDUP) }
	    "SMNT" sp @p1 string @p2 end { CMD_STRING(SMNT) }
	    "QUIT" end { CMD_NOPARAM(QUIT) }
	    "PORT" sp hostport end {
	        cmd.keyword = PORT;
	        cmd.parameter.numbers[0] = strtoul(p1, NULL, 10);
	        cmd.parameter.numbers[1] = strtoul(p2, NULL, 10);
	        cmd.parameter.numbers[2] = strtoul(p3, NULL, 10);
	        cmd.parameter.numbers[3] = strtoul(p4, NULL, 10);

	        cmd.parameter.numbers[4] = strtoul(p5, NULL, 10);
	        cmd.parameter.numbers[5] = strtoul(p6, NULL, 10);
	        goto done;
	    }
	    "PASV" end { CMD_NOPARAM(PASV) }
	    "TYPE" sp @p1 typecode end {
	        cmd.keyword = TYPE;
	        cmd.parameter.code = *p1;
	        goto done;
	    }
	    "STRU" sp @p1 structurecode end {
	        cmd.keyword = STRU;
	        cmd.parameter.code = *p1;
	        goto done;
	    }
	    "MODE" sp @p1 modecode end {
	        cmd.keyword = MODE;
	        cmd.parameter.code = *p1;
	        goto done;
	    }
	    "RETR" sp @p1 string @p2 end { CMD_STRING(RETR) }
	    "STOR" sp @p1 string @p2 end { CMD_STRING(STOR) }
	    "STOU" end { CMD_NOPARAM(STOU) }
	    "ALLO" end { CMD_NOPARAM(ALLO) } // No proper support
	    "REST" sp @p1 string @p2 end { CMD_STRING(REST) }
	    "RNFR" sp @p1 string @p2 end { CMD_STRING(RNFR) }
	    "RNTO" sp @p1 string @p2 end { CMD_STRING(RNTO) }
	    "ABOR" end { CMD_NOPARAM(ABOR) }
	    "DELE" sp @p1 string @p2 end { CMD_STRING(DELE) }
	    "RMD" sp @p1 string @p2 end { CMD_STRING(RMD) }
	    "MKD" sp @p1 string @p2 end { CMD_STRING(MKD) }
	    "PWD" end { CMD_NOPARAM(PWD) }
	    "LIST" (sp @p1 string @p2)? end { CMD_OPT_STRING(LIST) }
	    "NLST" (sp @p1 string @p2)? end { CMD_OPT_STRING(NLST) }
	    "SITE" (sp @p1 string @p2)? end { CMD_STRING(SITE) }
	    "SYST" end { CMD_NOPARAM(SYST) }
	    "STAT" (sp @p1 string @p2)? end { CMD_OPT_STRING(STAT) }
	    "HELP" (sp @p1 string @p2)? end { CMD_OPT_STRING(HELP) }
	    "NOOP" end { CMD_NOPARAM(NOOP) }
	*/
done:
	return cmd;
}

#ifdef TEST_PARSER

int main(int argc, char const *argv[]) {
	for (int i = 1; i < argc; i++) {
		printf("parsing %s\n", argv[i]);
		FtpCmd parsed = parse_ftpcmd(argv[i]);
		printf("parsed: %s\n", keyword_names[parsed.keyword]);
		parsed.parameter.string[MAX_STRSIZE] = 0;
		printf("string: \"%s\"\n", parsed.parameter.string);
		printf("numbers: %d,%d,%d-%d,%d\n", parsed.parameter.numbers[0],
		       parsed.parameter.numbers[1], parsed.parameter.numbers[2],
		       parsed.parameter.numbers[3], parsed.parameter.numbers[4]);
		printf("code: %c\n", parsed.parameter.code);
	}
	return 0;
}

#endif
// vim:syntax=c
