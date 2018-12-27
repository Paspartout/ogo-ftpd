# CC = musl-gcc
CFLAGS  := -Wall -Wextra -O0 -g
LDFLAGS := -flto

TARGET := uftpd
TARGETLIB := lib$(TARGET).a

%.c: %.re
	re2c -W -T -o $@ $^

all: $(TARGET) $(TARGETLIB)

$(TARGETLIB): uftpd.o cmdparser.o
	ar -rcs lib$(TARGET).a $^

$(TARGET): main.o $(TARGETLIB)

clean:
	rm -f *.o $(TARGET) $(TARGETLIB)

.SECONDARY: cmdparser.c
.PHONY: clean
