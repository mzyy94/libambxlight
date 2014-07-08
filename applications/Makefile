PROGRAM = ambxtest
SRCS = ambxtest.c
OBJS = $(subst .c,.o,$(SRCS))

CC = gcc
CFLAGS = -Wall -O2

$(PROGRAM): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -lm -lX11 -lambxlight -o $(PROGRAM) 

.c.o:
	$(CC) $(CFLAGS) $(LDFLAGS) -g -c $< 

.PHONY: clean

clean:
	rm $(OBJS) $(PROGRAM)
