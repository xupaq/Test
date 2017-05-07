CC = cc
CFLAGS = -Wall -std=c99 -D_POSIX_C_SOURCE=200809L -g
LDFLAGS = `pkg-config fuse --cflags --libs`

all:	wfsfuse

wfsfuse:	wfsfuse.c wfs.h
		$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
		rm -f wfsfuse
