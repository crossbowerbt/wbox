# Makefile for wbox
# Copyright (C) 2007 Salvatore Sanfilippo <antirez@invece.org>
# All Rights Reserved
# Under the New BSD license

DEBUG?= -g
CFLAGS?= -O2 -Wall -W
CCOPT= $(CFLAGS)

OBJ = anet.o sds.o wbsignal.o wbox.o
PRGNAME = wbox

all: wbox

wbox: $(OBJ)
	$(CC) -o $(PRGNAME) $(CCOPT) $(DEBUG) $(OBJ)

.c.o:
	$(CC) -c $(CCOPT) $(DEBUG) $(COMPILE_TIME) $<

clean:
	rm -rf $(PRGNAME) *.o

dep:
	$(CC) -MM *.c
