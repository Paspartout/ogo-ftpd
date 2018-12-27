uftpd - minimal ftp server
==========================

uftpd is my attempt at writing a minimal ftp server.
It aims to be as simple as possible to support the use case of
transferring and managing files between two hosts in a trusted local network.

I tested it on my Linux machine and used [FileZilla](https://filezilla-project.org/) as a client.

Limitations
-----------

The server currently has the following limitations:

- No authentication(every username and password gets accepted)
- No passive ftp
- No parallel transfers for multiple users
- Only File structure and Image(Binary) of Spec is supported
  (This doesn't seem to be a problem for most use cases though)

Feel free to fix these issues and open a pull request.

Building
--------

Simply type `make` to build the library and example server.

If you want to modify the command parser you have to install [re2c](http://re2c.org/)
which is used to generate the `cmdparser.c` file from `cmdparser.re`.

API
---

The server can be used as a library. 

Look at the `uftpd.h` file for the exposed functions and `main.c` for an example
on how to use them.

