# Makefile for ext2 utilities
CC      = gcc
CFLAGS  = -Wall -Wextra -g -std=c11 -D_FILE_OFFSET_BITS=64

BINS    = ext2info ext2cat ext2ls

.PHONY: all test clean valgrind

all: $(BINS)

ext2info: ext2info.c ext2.h
	$(CC) $(CFLAGS) -o $@ $<

ext2cat: ext2cat.c ext2.h
	$(CC) $(CFLAGS) -o $@ $<

ext2ls: ext2ls.c
	$(CC) $(CFLAGS) -o $@ $<

test: all
	bash test.sh

valgrind: all
	bash test.sh valgrind

clean:
	rm -f $(BINS)